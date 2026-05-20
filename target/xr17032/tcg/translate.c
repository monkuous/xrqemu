/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/17032 emulation for QEMU - main translation routines.
 *
 * Copyright (c) 2026 monkuous
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/translation-block.h"
#include "exec/translator.h"
#include "exec/target_page.h"
#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"
#include "qemu/qemu-print.h"
#include "tcg_xr17032.h"
#include "translate.h"
#include "internals.h"
#include "cr.h"

/* Global register indices */
TCGv cpu_gpr[32], cpu_pc;
static TCGv cpu_lladdr, cpu_llval;

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

#define DISAS_STOP        DISAS_TARGET_0
#define DISAS_EXIT        DISAS_TARGET_1
#define DISAS_EXIT_UPDATE DISAS_TARGET_2

static inline int shl_1(DisasContext *ctx, int x)
{
    return x << 1;
}

static inline int shl_2(DisasContext *ctx, int x)
{
    return x << 2;
}

static inline int shl_16(DisasContext *ctx, int x)
{
    return x << 16;
}

static void generate_exception(DisasContext *ctx, int excp)
{
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    gen_helper_raise_exception(tcg_env, tcg_constant_i32(excp));
    ctx->base.is_jmp = DISAS_NORETURN;
}

static void gen_goto_tb(DisasContext *ctx, unsigned tb_slot_idx, vaddr dest)
{
    if (translator_use_goto_tb(&ctx->base, dest)) {
        tcg_gen_goto_tb(tb_slot_idx);
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_exit_tb(ctx->base.tb, tb_slot_idx);
    } else {
        tcg_gen_movi_tl(cpu_pc, dest);
        tcg_gen_lookup_and_goto_ptr();
    }
}

static void xr17032_tr_init_disas_context(DisasContextBase *dcbase,
                                          CPUState *cs)
{
    int64_t bound;
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->page_start = ctx->base.pc_first & TARGET_PAGE_MASK;
    ctx->plv = ctx->base.tb->flags & HW_FLAGS_PLV_MASK;
    if (ctx->base.tb->flags & HW_FLAGS_RS_M) {
        ctx->mem_idx = ctx->plv * 2;
    } else {
        ctx->mem_idx = MMU_DIRECT;
    }

    /* Bound the number of insns to execute to those left on the page.  */
    bound = -(ctx->base.pc_first | TARGET_PAGE_MASK) / 4;
    ctx->base.max_insns = MIN(ctx->base.max_insns, bound);

    if (ctx->base.tb->flags & HW_FLAGS_RS_T) {
        ctx->zero = cpu_gpr[0];
    } else {
        ctx->zero = tcg_constant_tl(0);
    }
}

static void xr17032_tr_tb_start(DisasContextBase *dcbase, CPUState *cs)
{
}

static void xr17032_tr_insn_start(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(ctx->base.pc_next, 0, 0);
}

static TCGv gpr_src(DisasContext *ctx, int reg_num)
{
    if (reg_num == 0) {
        return ctx->zero;
    }

    return cpu_gpr[reg_num];
}

static TCGv gpr_dst(DisasContext *ctx, int reg_num)
{
    if (reg_num == 0 && (ctx->base.tb->flags & HW_FLAGS_RS_T) == 0) {
        return tcg_temp_new();
    }

    return cpu_gpr[reg_num];
}

static void gen_set_gpr(DisasContext *ctx, int reg_num, TCGv t)
{
    if (reg_num != 0 || (ctx->base.tb->flags & HW_FLAGS_RS_T) != 0) {
        tcg_gen_mov_tl(cpu_gpr[reg_num], t);
    }
}

static TCGv make_address_x(DisasContext *ctx, TCGv base, TCGv addend)
{
    TCGv temp = NULL;

    if (addend) {
        temp = tcg_temp_new();
        tcg_gen_add_tl(temp, base, addend);
        base = temp;
    }

    return base;
}

static TCGv make_address_i(DisasContext *ctx, TCGv base, target_long ofs)
{
    TCGv addend = ofs ? tcg_constant_tl(ofs) : NULL;
    return make_address_x(ctx, base, addend);
}

static void gen_slt(TCGv dest, TCGv src1, TCGv src2)
{
    tcg_gen_setcond_tl(TCG_COND_LTU, dest, src1, src2);
}

static void gen_slts(TCGv dest, TCGv src1, TCGv src2)
{
    tcg_gen_setcond_tl(TCG_COND_LT, dest, src1, src2);
}

static TCGv make_shifted(TCGv base, DisasShift shfunc, target_ulong shamt)
{
    TCGv temp = NULL;

    shamt &= 31;

    if (shamt) {
        temp = tcg_temp_new();

        switch (shfunc) {
        case SHIFT_LEFT:
            tcg_gen_shli_tl(temp, base, shamt);
            break;
        case SHIFT_RIGHT:
            tcg_gen_shri_tl(temp, base, shamt);
            break;
        case SHIFT_ARITHMETIC:
            tcg_gen_sari_tl(temp, base, shamt);
            break;
        case SHIFT_ROTATE:
            tcg_gen_rotri_tl(temp, base, shamt);
            break;
        default:
            g_assert_not_reached();
        }

        base = temp;
    }

    return base;
}

#include "decode-insns.c.inc"

#pragma region Jump instructions

static bool trans_jal(DisasContext *ctx, arg_jal *a)
{
    tcg_gen_movi_tl(cpu_gpr[XR17032_LR], ctx->base.pc_next + 4);
    gen_goto_tb(ctx, 0, (ctx->base.pc_next & 0x80000000u) | a->imm);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_j(DisasContext *ctx, arg_j *a)
{
    gen_goto_tb(ctx, 0, (ctx->base.pc_next & 0x80000000u) | a->imm);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

#pragma endregion

#pragma region Branch instructions

static bool gen_bc(DisasContext *ctx, arg_b *a, TCGCond cond, target_long value)
{
    TCGLabel *l = gen_new_label();
    tcg_gen_brcondi_tl(cond, gpr_src(ctx, a->ra), value, l);
    gen_goto_tb(ctx, 1, ctx->base.pc_next + 4);
    gen_set_label(l);
    gen_goto_tb(ctx, 0, ctx->base.pc_next + a->imm);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

TRANS(beq, ALL, gen_bc, TCG_COND_EQ, 0)
TRANS(bne, ALL, gen_bc, TCG_COND_NE, 0)
TRANS(blt, ALL, gen_bc, TCG_COND_LT, 0)
TRANS(bgt, ALL, gen_bc, TCG_COND_GT, 0)
TRANS(ble, ALL, gen_bc, TCG_COND_LE, 0)
TRANS(bge, ALL, gen_bc, TCG_COND_GE, 0)
TRANS(bpe, ALL, gen_bc ,TCG_COND_TSTEQ, 1)
TRANS(bpo, ALL, gen_bc ,TCG_COND_TSTNE, 1)

#pragma endregion

#pragma region Immediate arithmetic instructions

static bool gen_rri(DisasContext *ctx, arg_i *a, void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->ra);
    TCGv src1 = gpr_src(ctx, a->rb);
    TCGv src2 = tcg_constant_tl(a->imm);

    func(dest, src1, src2);
    gen_set_gpr(ctx, a->ra, dest);

    return true;
}

TRANS(addi, ALL, gen_rri, tcg_gen_add_tl)
TRANS(subi, ALL, gen_rri, tcg_gen_sub_tl)
TRANS(slti, ALL, gen_rri, gen_slt)
TRANS(sltis, ALL, gen_rri, gen_slts)
TRANS(andi, ALL, gen_rri, tcg_gen_and_tl)
TRANS(xori, ALL, gen_rri, tcg_gen_xor_tl)
TRANS(ori, ALL, gen_rri, tcg_gen_or_tl)
TRANS(lui, ALL, gen_rri, tcg_gen_or_tl)

#pragma endregion

#pragma region Immediate load instructions

static bool gen_loadi(DisasContext *ctx, arg_i *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->ra);
    TCGv addr = gpr_src(ctx, a->rb);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(ctx, a->ra, dest);

    return true;
}

TRANS(mov1_reg_imm, ALL, gen_loadi, MO_UB)
TRANS(mov2_reg_imm, ALL, gen_loadi, MO_LEUW | MO_ALIGN)
TRANS(mov4_reg_imm, ALL, gen_loadi, MO_LEUL | MO_ALIGN)

#pragma endregion

#pragma region Immediate store instructions

static bool gen_storei(DisasContext *ctx, arg_i *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->rb);
    TCGv addr = gpr_src(ctx, a->ra);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);
    return true;
}

TRANS(mov1_imm_reg, ALL, gen_storei, MO_UB)
TRANS(mov2_imm_reg, ALL, gen_storei, MO_LEUW | MO_ALIGN)
TRANS(mov4_imm_reg, ALL, gen_storei, MO_LEUL | MO_ALIGN)

static bool gen_storem(DisasContext *ctx, arg_m *a, MemOp mop)
{
    TCGv data = tcg_constant_tl(a->val);
    TCGv addr = gpr_src(ctx, a->ra);

    addr = make_address_i(ctx, addr, a->imm);

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);
    return true;
}

TRANS(mov1_imm_val, ALL, gen_storem, MO_UB)
TRANS(mov2_imm_val, ALL, gen_storem, MO_LEUW | MO_ALIGN)
TRANS(mov4_imm_val, ALL, gen_storem, MO_LEUL | MO_ALIGN)

#pragma endregion

#pragma region Miscellaneous immediate instructions

static bool trans_jalr(DisasContext *ctx, arg_jalr *a)
{
    TCGv dest = gpr_dst(ctx, a->ra);
    TCGv src1 = gpr_src(ctx, a->rb);

    TCGv addr = make_address_i(ctx, src1, a->imm);
    tcg_gen_mov_tl(cpu_pc, addr);
    tcg_gen_movi_tl(dest, ctx->base.pc_next + 4);
    gen_set_gpr(ctx, a->ra, dest);
    tcg_gen_lookup_and_goto_ptr();
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_adr(DisasContext *ctx, arg_adr *a)
{
    TCGv dest = gpr_dst(ctx, a->ra);

    tcg_gen_movi_tl(dest, ctx->base.pc_first + a->imm);
    gen_set_gpr(ctx, a->ra, dest);
    return true;
}

#pragma endregion

#pragma region Register memory instructions

static bool gen_loadr(DisasContext *ctx, arg_r *a, MemOp mop)
{
    TCGv dest = gpr_dst(ctx, a->ra);
    TCGv addr = gpr_src(ctx, a->rb);

    addr = make_address_x(ctx, addr, 
        make_shifted(gpr_src(ctx, a->rc), a->shfunc, a->shamt));

    tcg_gen_qemu_ld_tl(dest, addr, ctx->mem_idx, mop);
    gen_set_gpr(ctx, a->ra, dest);

    return true;
}

TRANS(mov1_reg_off, ALL, gen_loadr, MO_UB)
TRANS(mov2_reg_off, ALL, gen_loadr, MO_LEUW | MO_ALIGN)
TRANS(mov4_reg_off, ALL, gen_loadr, MO_LEUL | MO_ALIGN)

static bool gen_storer(DisasContext *ctx, arg_r *a, MemOp mop)
{
    TCGv data = gpr_src(ctx, a->ra);
    TCGv addr = gpr_src(ctx, a->rb);

    addr = make_address_x(ctx, addr, 
        make_shifted(gpr_src(ctx, a->rc), a->shfunc, a->shamt));

    tcg_gen_qemu_st_tl(data, addr, ctx->mem_idx, mop);
    return true;
}

TRANS(mov1_off_reg, ALL, gen_storer, MO_UB)
TRANS(mov2_off_reg, ALL, gen_storer, MO_LEUW | MO_ALIGN)
TRANS(mov4_off_reg, ALL, gen_storer, MO_LEUL | MO_ALIGN)

#pragma endregion

#pragma region Register arithmetic instructions

static bool gen_shift(DisasContext *ctx, arg_r3 *a, void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->ra);
    TCGv src1 = gpr_src(ctx, a->rc);
    TCGv src2 = gpr_src(ctx, a->rb);
    TCGv t0 = tcg_temp_new();

    tcg_gen_andi_tl(t0, src2, 31);
    func(dest, src1, src2);
    gen_set_gpr(ctx, a->ra, dest);

    return true;
}

TRANS(lsh, ALL, gen_shift, tcg_gen_shl_tl)
TRANS(rsh, ALL, gen_shift, tcg_gen_shr_tl)
TRANS(ash, ALL, gen_shift, tcg_gen_sar_tl)
TRANS(ror, ALL, gen_shift, tcg_gen_rotr_tl)

static bool gen_rrr(DisasContext *ctx, arg_r *a, void (*func)(TCGv, TCGv, TCGv))
{
    TCGv dest = gpr_dst(ctx, a->ra);
    TCGv src1 = gpr_src(ctx, a->rb);
    TCGv src2 = make_shifted(gpr_src(ctx, a->rc), a->shfunc, a->shamt);

    func(dest, src1, src2);
    gen_set_gpr(ctx, a->ra, dest);

    return true;
}

TRANS(add, ALL, gen_rrr, tcg_gen_add_tl)
TRANS(sub, ALL, gen_rrr, tcg_gen_sub_tl)
TRANS(slt, ALL, gen_rrr, gen_slt)
TRANS(slts, ALL, gen_rrr, gen_slts)
TRANS(and, ALL, gen_rrr, tcg_gen_and_tl)
TRANS(xor, ALL, gen_rrr, tcg_gen_xor_tl)
TRANS(or, ALL, gen_rrr, tcg_gen_or_tl)
TRANS(nor, ALL, gen_rrr, tcg_gen_nor_tl)

#pragma endregion

#pragma region Miscellaneous unprivileged instructions

TRANS(mul, ALL, gen_rrr, tcg_gen_mul_tl)
TRANS(div, ALL, gen_rrr, tcg_gen_divu_tl)
TRANS(divs, ALL, gen_rrr, tcg_gen_div_tl)
TRANS(mod, ALL, gen_rrr, tcg_gen_remu_tl)

static bool trans_ll(DisasContext *ctx, arg_ll *a)
{
    TCGv t1 = tcg_temp_new();
    TCGv src1 = gpr_src(ctx, a->rb);

    tcg_gen_qemu_ld_tl(t1, src1, ctx->mem_idx, MO_LEUL | MO_ALIGN);
    tcg_gen_st_tl(src1, tcg_env, offsetof(CPUXR17032State, lladdr));
    tcg_gen_st_tl(t1, tcg_env, offsetof(CPUXR17032State, llval));
    gen_set_gpr(ctx, a->ra, t1);

    return true;
}

static bool trans_sc(DisasContext *ctx, arg_sc *a)
{
    TCGv dest = gpr_dst(ctx, a->ra);
    TCGv src1 = gpr_src(ctx, a->rb);
    TCGv src2 = gpr_src(ctx, a->rc);
    TCGv val = tcg_temp_new();

    TCGLabel *l1 = gen_new_label();
    TCGLabel *done = gen_new_label();

    tcg_gen_brcond_tl(TCG_COND_EQ, src1, cpu_lladdr, l1);
    tcg_gen_movi_tl(dest, 0);
    tcg_gen_br(done);

    gen_set_label(l1);
    tcg_gen_mov_tl(val, src2);
    /* generate cmpxchg */
    tcg_gen_atomic_cmpxchg_tl(src1, cpu_lladdr, cpu_llval,
                              val, ctx->mem_idx, MO_LEUL | MO_ALIGN);
    tcg_gen_setcond_tl(TCG_COND_EQ, dest, src1, cpu_llval);
    gen_set_label(done);
    gen_set_gpr(ctx, a->ra, dest);

    return true;
}

static bool trans_pause(DisasContext *ctx, arg_pause *a)
{
    /*
     * PAUSE is a no-op in QEMU,
     * end the TB and return to main loop
     */
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next + 4);
    tcg_gen_exit_tb(NULL, 0);
    ctx->base.is_jmp = DISAS_NORETURN;

    return true;
}

static bool trans_mb(DisasContext *ctx, arg_mb *a)
{
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);
    return true;
}

static bool trans_wmb(DisasContext *ctx, arg_wmb *a)
{
    tcg_gen_mb(TCG_BAR_STRL | TCG_MO_ALL);
    return true;
}

static bool trans_brk(DisasContext *ctx, arg_brk *a)
{
    generate_exception(ctx, EXCCODE_BRK);
    return true;
}

static bool trans_sys(DisasContext *ctx, arg_sys *a)
{
    generate_exception(ctx, EXCCODE_SYS);
    return true;
}

#pragma endregion

#pragma region Privileged instructions

#ifdef CONFIG_USER_ONLY

#define GEN_FALSE_TRANS(name)   \
static bool trans_##name(DisasContext *ctx, arg_##name * a)  \
{   \
    return false;   \
}

GEN_FALSE_TRANS(mfcr)
GEN_FALSE_TRANS(mtcr)
GEN_FALSE_TRANS(hlt)
GEN_FALSE_TRANS(rfe)

#else

typedef void (*GenCRRead)(TCGv dest, TCGv_ptr env);
typedef void (*GenCRWrite)(TCGv_ptr env, TCGv src);

static bool check_plv(DisasContext *ctx)
{
    if (ctx->plv != MMU_PLV_KERNEL) {
        generate_exception(ctx, EXCCODE_PRV);
        return true;
    }

    return false;
}

static bool set_cr_trans_func(unsigned int csr_num, GenCRRead readfn,
                              GenCRWrite writefn)
{
    CRInfo *csr;

    csr = get_cr(csr_num);
    if (!csr) {
        return false;
    }

    csr->readfn = (GenCRFunc)readfn;
    csr->writefn = (GenCRFunc)writefn;
    return true;
}

#define SET_CR_FUNC(NAME, read, write)                 \
        set_cr_trans_func(XR17032_CR_##NAME, read, write)

void xr17032_cr_translate_init(void)
{
    SET_CR_FUNC(ITBPTE, NULL, gen_helper_crwr_itbpte);
    SET_CR_FUNC(DTBPTE, NULL, gen_helper_crwr_dtbpte);
    SET_CR_FUNC(ITBINDEX, NULL, gen_helper_crwr_itbindex);
    SET_CR_FUNC(DTBINDEX, NULL, gen_helper_crwr_dtbindex);
    SET_CR_FUNC(ITBCTRL, NULL, gen_helper_crwr_itbctrl);
    SET_CR_FUNC(DTBCTRL, NULL, gen_helper_crwr_dtbctrl);
}
#undef SET_CR_FUNC

static bool check_cr_flags(DisasContext *ctx, const CRInfo *cr, bool write)
{
    if (write) {
        if (cr->flags & CRFL_READONLY) {
            return false;
        }

        if (cr->flags & CRFL_EXITTB) {
            ctx->base.is_jmp = DISAS_EXIT_UPDATE;
        }

        if (cr->flags & CRFL_STOP) {
            ctx->base.is_jmp = DISAS_STOP;
        }
    }

    return true;
}

static bool trans_mfcr(DisasContext *ctx, arg_mfcr *a)
{
    TCGv dest;
    const CRInfo *cr;
    GenCRRead readfn;

    if (check_plv(ctx)) {
        return false;
    }

    cr = get_cr(a->cr);

    if (cr == NULL) {
        /* CR is undefined: read as 0. */
        dest = tcg_constant_tl(0);
    } else {
        check_cr_flags(ctx, cr, false);
        dest = gpr_dst(ctx, a->gr);
        readfn = (GenCRRead)cr->readfn;

        if (readfn) {
            readfn(dest, tcg_env);
        } else {
            tcg_gen_ld_tl(dest, tcg_env, cr->offset);
        }
    }

    gen_set_gpr(ctx, a->gr, dest);
    return true;
}

static bool trans_mtcr(DisasContext *ctx, arg_mtcr *a)
{
    TCGv src1;
    const CRInfo *cr;
    GenCRWrite writefn;

    if (check_plv(ctx)) {
        return false;
    }

    cr = get_cr(a->cr);

    if (cr == NULL) {
        /* CR is undefined: write ignored. */
        return true;
    }

    if (!check_cr_flags(ctx, cr, true)) {
        /* CSR is readonly: write ignored. */
        return true;
    }

    src1 = gpr_src(ctx, a->gr);
    writefn = (GenCRWrite)cr->writefn;

    if (writefn) {
        writefn(tcg_env, src1);
    } else {
        tcg_gen_st_tl(src1, tcg_env, cr->offset);
    }

    if ((cr->flags & CRFL_TLBFILL) && ctx->mem_idx != MMU_DIRECT) {
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next + 4);
        ctx->base.is_jmp = DISAS_EXIT;
    }

    return true;
}

static bool trans_hlt(DisasContext *ctx, arg_hlt *a)
{
    if (check_plv(ctx)) {
        return false;
    }

    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next + 4);
    gen_helper_hlt(tcg_env);
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_rfe(DisasContext *ctx, arg_rfe *a)
{
    if (check_plv(ctx)) {
        return false;
    }

    gen_helper_rfe(tcg_env);
    ctx->base.is_jmp = DISAS_EXIT;
    return true;
}

#endif

#pragma endregion

static void xr17032_tr_translate_insn(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    ctx->opcode = translator_ldl_end(cpu_env(cs), &ctx->base,
                                     ctx->base.pc_next, MO_LE);

    if (!decode(ctx, ctx->opcode)) {
        qemu_log_mask(LOG_UNIMP, "Error: unknown opcode. "
                      "0x%" VADDR_PRIx ": 0x%x\n",
                      ctx->base.pc_next, ctx->opcode);
        generate_exception(ctx, EXCCODE_INV);
    }

    ctx->base.pc_next += 4;
}

static void xr17032_tr_tb_stop(DisasContextBase *dcbase, CPUState *cs)
{
    DisasContext *ctx = container_of(dcbase, DisasContext, base);

    switch (ctx->base.is_jmp) {
    case DISAS_STOP:
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
        tcg_gen_lookup_and_goto_ptr();
        break;
    case DISAS_TOO_MANY:
        gen_goto_tb(ctx, 0, ctx->base.pc_next);
        break;
    case DISAS_NORETURN:
        break;
    case DISAS_EXIT_UPDATE:
        tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
        QEMU_FALLTHROUGH;
    case DISAS_EXIT:
        tcg_gen_exit_tb(NULL, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static const TranslatorOps xr17032_tr_ops = {
    .init_disas_context = xr17032_tr_init_disas_context,
    .tb_start           = xr17032_tr_tb_start,
    .insn_start         = xr17032_tr_insn_start,
    .translate_insn     = xr17032_tr_translate_insn,
    .tb_stop            = xr17032_tr_tb_stop,
};

void xr17032_translate_code(CPUState *cs, TranslationBlock *tb,
                            int *max_insns, vaddr pc, void *host_pc)
{
    DisasContext ctx;

    translator_loop(cs, tb, max_insns, pc, host_pc,
                    &xr17032_tr_ops, &ctx.base);
}

void xr17032_translate_init(void)
{
    int i;

    for (i = 0; i < 32; i++) {
        cpu_gpr[i] = tcg_global_mem_new(tcg_env,
                                        offsetof(CPUXR17032State, gpr[i]),
                                        regnames[i]);
    }

    cpu_pc = tcg_global_mem_new(tcg_env, offsetof(CPUXR17032State, pc), "pc");
    cpu_lladdr = tcg_global_mem_new(tcg_env,
                    offsetof(CPUXR17032State, lladdr), "lladdr");
    cpu_llval = tcg_global_mem_new(tcg_env,
                    offsetof(CPUXR17032State, llval), "llval");

#ifndef CONFIG_USER_ONLY
    xr17032_cr_translate_init();
#endif
}

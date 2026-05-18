/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU XR/17032 Disassembler
 *
 * Copyright (c) 2026 monkuous
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"
#include "qemu/bitops.h"
#include "cpu-cr.h"
#include "cpu.h"

typedef struct {
    disassemble_info *info;
    uint64_t pc;
    uint32_t insn;
} DisasContext;

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

#define CR_NAME(REG) \
    [XR17032_CR_##REG] = (#REG)

static const char * const cr_names[] = {
    CR_NAME(RS),
    CR_NAME(WHAMI),
    CR_NAME(EB),
    CR_NAME(EPC),
    CR_NAME(EBADADDR),
    CR_NAME(TBMISSADDR),
    CR_NAME(TBPC),
    CR_NAME(SCRATCH0),
    CR_NAME(SCRATCH1),
    CR_NAME(SCRATCH2),
    CR_NAME(SCRATCH3),
    CR_NAME(SCRATCH4),
    CR_NAME(ITBPTE),
    CR_NAME(ITBTAG),
    CR_NAME(ITBINDEX),
    CR_NAME(ITBCTRL),
    CR_NAME(ICACHECTRL),
    CR_NAME(ITBADDR),
    CR_NAME(DTBPTE),
    CR_NAME(DTBTAG),
    CR_NAME(DTBINDEX),
    CR_NAME(DTBCTRL),
    CR_NAME(DCACHECTRL),
    CR_NAME(DTBADDR),
};

static const char * const shfunc_names[4] = {
    "LSH",
    "RSH",
    "ASH",
    "ROR",
};

static const char *get_cr_name(unsigned num)
{
    return ((num < ARRAY_SIZE(cr_names)) && (cr_names[num] != NULL)) ?
           cr_names[num] : "Undefined CR";
}

#define output(C, INSN, FMT, ...)                                      \
 {                                                                     \
    if ((C)->info->show_opcodes) {                                     \
        (C)->info->fprintf_func((C)->info->stream, "%08x   %-9s\t" FMT,\
                            (C)->insn, INSN, ##__VA_ARGS__);           \
    } else {                                                           \
        (C)->info->fprintf_func((C)->info->stream, "%-9s\t" FMT,       \
                            INSN, ##__VA_ARGS__);                      \
    }                                                                  \
}

#include "decode-insns.c.inc"

int print_insn_xr17032(bfd_vma memaddr, struct disassemble_info *info)
{
    bfd_byte buffer[4];
    uint32_t insn;
    int status;

    status = (*info->read_memory_func)(memaddr, buffer, 4, info);
    if (status != 0) {
        (*info->memory_error_func)(status, memaddr, info);
        return -1;
    }
    insn = bfd_getl32(buffer);
    DisasContext ctx = {
        .info = info,
        .pc = memaddr,
        .insn = insn
    };

    if (!decode(&ctx, insn)) {
        output(&ctx, "illegal", "");
    }
    return 4;
}

static void output_j(DisasContext *ctx, arg_j *a, const char *mnemonic)
{
    output(ctx, mnemonic, "0x%x", a->imm | (ctx->pc & 0x80000000u));
}

static void output_b(DisasContext *ctx, arg_b *a, const char *mnemonic)
{
    output(ctx, mnemonic, "%s, 0x%x", regnames[a->ra], ctx->pc + a->imm);
}

static void output_i(DisasContext *ctx, arg_i *a, const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, 0x%x", regnames[a->ra], regnames[a->rb], a->imm);
}

static void output_is(DisasContext *ctx, arg_i *a, const char *mnemonic)
{
    if (a->imm >= 0) {
        output(ctx, mnemonic, "%s, %s, 0x%x", regnames[a->ra], regnames[a->rb], a->imm);
    } else {
        output(ctx, mnemonic, "%s, %s, -0x%x", regnames[a->ra], regnames[a->rb], -a->imm);
    }
}

static void output_li(DisasContext *ctx, arg_i *a, const char *mnemonic, const char *size)
{
    output(ctx, mnemonic, "%s, %s [%s + 0x%x]", regnames[a->ra], size, regnames[a->rb], a->imm);
}

static void output_si(DisasContext *ctx, arg_i *a, const char *mnemonic, const char *size)
{
    output(ctx, mnemonic, "%s [%s + 0x%x], %s", size, regnames[a->ra], a->imm, regnames[a->rb]);
}

static void output_sm(DisasContext *ctx, arg_m *a, const char *mnemonic, const char *size)
{
    output(ctx, mnemonic, "%s [%s + 0x%x], %d", size, regnames[a->ra], a->imm, a->val);
}

static void output_lr(DisasContext *ctx, arg_r *a, const char *mnemonic, const char *size)
{
    output(ctx, mnemonic, "%s, %s [%s + %s %s %d]", regnames[a->ra], size, regnames[a->rb], regnames[a->rc], shfunc_names[a->shfunc], a->shamt);
}

static void output_sr(DisasContext *ctx, arg_r *a, const char *mnemonic, const char *size)
{
    output(ctx, mnemonic, "%s [%s + %s %s %d], %s", size, regnames[a->rb], regnames[a->rc], shfunc_names[a->shfunc], a->shamt, regnames[a->ra]);
}

static void output_r3r(DisasContext *ctx, arg_r3 *a, const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s", regnames[a->ra], regnames[a->rc], regnames[a->rb]);
}

static void output_r(DisasContext *ctx, arg_r *a, const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s %s %d", regnames[a->ra], regnames[a->rb], regnames[a->rc], shfunc_names[a->shfunc], a->shamt);
}

static void output_r2(DisasContext *ctx, arg_r2 *a, const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", regnames[a->ra], regnames[a->rb]);
}

static void output_r3(DisasContext *ctx, arg_r3 *a, const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s, %s", regnames[a->ra], regnames[a->rb], regnames[a->rc]);
}

static void output_empty(DisasContext *ctx, arg_empty *a,
                         const char *mnemonic)
{
    output(ctx, mnemonic, "");
}

static void output_mfcr(DisasContext *ctx, arg_cr *a, const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", regnames[a->gr], get_cr_name(a->cr));
}

static void output_mtcr(DisasContext *ctx, arg_cr *a, const char *mnemonic)
{
    output(ctx, mnemonic, "%s, %s", get_cr_name(a->cr), regnames[a->gr]);
}

#define INSN(insn, type, ...)                               \
static bool trans_##insn(DisasContext *ctx, arg_##insn * a) \
{                                                           \
    output_##type(ctx, a, __VA_ARGS__);                     \
    return true;                                            \
}

#define DINSN(insn, type) INSN(insn, type, #insn)

DINSN(jal, j)
DINSN(j, j)
DINSN(beq, b)
DINSN(bne, b)
DINSN(blt, b)
DINSN(bgt, b)
DINSN(ble, b)
DINSN(bge, b)
DINSN(bpe, b)
DINSN(bpo, b)
DINSN(addi, i)
DINSN(subi, i)
DINSN(slti, i)
INSN(sltis, is, "slti signed")
DINSN(andi, i)
DINSN(xori, i)
DINSN(ori, i)
DINSN(lui, i)
INSN(mov1_reg_imm, li, "mov", "byte")
INSN(mov2_reg_imm, li, "mov", "int")
INSN(mov4_reg_imm, li, "mov", "long")
INSN(mov1_imm_reg, si, "mov", "byte")
INSN(mov2_imm_reg, si, "mov", "int")
INSN(mov4_imm_reg, si, "mov", "long")
INSN(mov1_imm_val, sm, "mov", "byte")
INSN(mov2_imm_val, sm, "mov", "int")
INSN(mov4_imm_val, sm, "mov", "long")
DINSN(jalr, i)
DINSN(adr, b)
INSN(mov1_reg_off, lr, "mov", "byte")
INSN(mov2_reg_off, lr, "mov", "int")
INSN(mov4_reg_off, lr, "mov", "long")
INSN(mov1_off_reg, sr, "mov", "byte")
INSN(mov2_off_reg, sr, "mov", "int")
INSN(mov4_off_reg, sr, "mov", "long")
DINSN(lsh, r3r)
DINSN(rsh, r3r)
DINSN(ash, r3r)
DINSN(ror, r3r)
DINSN(add, r)
DINSN(sub, r)
DINSN(slt, r)
INSN(slts, r, "slt signed")
DINSN(and, r)
DINSN(xor, r)
DINSN(or, r)
DINSN(nor, r)
DINSN(mul, r)
DINSN(div, r)
INSN(divs, r, "div signed")
DINSN(mod, r)
DINSN(ll, r2)
DINSN(sc, r3)
DINSN(pause, empty)
DINSN(mb, empty)
DINSN(wmb, empty)
DINSN(brk, empty)
DINSN(sys, empty)
DINSN(mfcr, mfcr)
DINSN(mtcr, mtcr)
DINSN(hlt, empty)
DINSN(rfe, empty)

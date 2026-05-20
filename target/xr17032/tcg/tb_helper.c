/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU XR/17032 TLB helpers
 *
 * Copyright (c) 2026 monkuous
 *
 */

#include "qemu/osdep.h"
#include "qemu/guest-random.h"

#include "cpu.h"
#include "cpu-mmu.h"
#include "internals.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "exec/page-protection.h"
#include "exec/target_page.h"
#include "accel/tcg/cpu-ldst.h"
#include "exec/log.h"
#include "cpu-cr.h"
#include "tcg/tcg_xr17032.h"

typedef bool (*tb_match)(bool global, int asid, int tb_asid);

static MMUIdxMap tlb_map(bool insn) {
    if (insn) {
        return (1 << MMU_KERNEL_INSN) | (1 << MMU_USER_INSN);
    } else {
        return (1 << MMU_KERNEL_DATA) | (1 << MMU_USER_DATA);
    }
}

static bool tb_match_accessible(bool global, int asid, int tb_asid)
{
    return global || tb_asid == asid;
}

static bool tb_match_any(bool global, int asid, int tb_asid)
{
    return true;
}

static void raise_mmu_exception(CPUXR17032State *env, vaddr address,
                                MMUAccessType access_type, TLBRet tlb_error)
{
    CPUState *cs = env_cpu(env);

    switch (tlb_error) {
    case TLBRET_NOMATCH:
        /* No TLB match for a mapped address */
        if (access_type == MMU_DATA_LOAD || access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_DTB;
            env->CR_DTBTAG = FIELD_DP32(env->CR_DTBTAG, CR_TBTAG, VFN,
                address >> TARGET_PAGE_BITS);
            env->CR_DTBADDR = FIELD_DP32(env->CR_DTBADDR, CR_TBADDR, VFN,
                address >> TARGET_PAGE_BITS);
        } else if (access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_ITB;
            env->CR_ITBTAG = FIELD_DP32(env->CR_ITBTAG, CR_TBTAG, VFN,
                address >> TARGET_PAGE_BITS);
            env->CR_ITBADDR = FIELD_DP32(env->CR_ITBADDR, CR_TBADDR, VFN,
                address >> TARGET_PAGE_BITS);
        }
        break;
    case TLBRET_INVALID:
        /* TLB match with no valid bit */
        if (access_type == MMU_DATA_LOAD || access_type == MMU_INST_FETCH) {
            cs->exception_index = EXCCODE_PGF;
        } else if (access_type == MMU_DATA_STORE) {
            cs->exception_index = EXCCODE_PFW;
        }
        break;
    case TLBRET_WI:
        /* Write-Inhibit Exception */
        cs->exception_index = EXCCODE_PFW;
        break;
    case TLBRET_PE:
        /* Privileged Exception */
        cs->exception_index = EXCCODE_PRV;
        break;
    default:
        g_assert_not_reached();
    }

    if (tlb_error == TLBRET_NOMATCH) {
        if (!FIELD_EX32(env->CR_RS, CR_RS, T)) {
            env->tb_miss_was_write = access_type == MMU_DATA_STORE;
            env->CR_TBMISSADDR = address;
        }
    } else {
        if (FIELD_EX32(env->CR_RS, CR_RS, T)) {
            cs->exception_index = env->tb_miss_was_write ? EXCCODE_PFW
                                                         : EXCCODE_PGF;
            env->CR_EBADADDR = env->CR_TBMISSADDR;
        } else {
            env->CR_EBADADDR = address;
        }
   }
}

static void invalidate_tb_entry(CPUXR17032State *env, int index, bool insn)
{
    target_ulong addr;
    XR17032TB *tb = insn ? &env->itb[index] : &env->dtb[index];
    uint32_t tb_vpn = FIELD_EX32(tb->tb_tag, CR_TBTAG, VFN);
    bool tb_v;

    addr = (tb_vpn << TARGET_PAGE_BITS) & TARGET_PAGE_MASK;
    addr = sextract64(addr, 0, TARGET_VIRT_ADDR_SPACE_BITS);

    tb_v = pte_present(env, tb->tb_pte);
    if (tb_v) {
        tlb_flush_page_by_mmuidx(env_cpu(env), addr, tlb_map(insn));
    }
}

static void invalidate_tb(CPUXR17032State *env, int index, bool insn)
{
    XR17032TB *tb;
    uint16_t cr_asid, tb_asid, tb_g;

    cr_asid = FIELD_EX32(insn ? env->CR_ITBTAG : env->CR_DTBTAG, CR_TBTAG, ASID);
    tb = insn ? &env->itb[index] : &env->dtb[index];
    tb_asid = FIELD_EX32(tb->tb_tag, CR_TBTAG, ASID);
    if (tb_asid == CR_TBTAG_ASID_INVALID) {
        return;
    }

    tb->tb_tag = FIELD_DP32(tb->tb_tag, CR_TBTAG, ASID, CR_TBTAG_ASID_INVALID);
    tb_g = FIELD_EX32(tb->tb_pte, CR_TBPTE, G);
    /* QEMU TLB is flushed when asid is changed */
    if (tb_g == 0 && tb_asid != cr_asid) {
        return;
    }
    invalidate_tb_entry(env, index, insn);
}

/* Prepare tlb entry information */
static void prepare_context(CPUXR17032State *env, MMUContext *context,
                            bool insn, uint32_t pte)
{
    uint32_t cr_vfn;

    cr_vfn = FIELD_EX32(insn ? env->CR_ITBTAG : env->CR_DTBTAG, CR_TBTAG, VFN);

    context->addr = cr_vfn << TARGET_PAGE_BITS;
    context->pte = pte;
}

static void fill_tb_entry(CPUXR17032State *env, XR17032TB *tb,
                          MMUContext *context, bool insn)
{
    uint32_t pte, cr_vfn;
    uint16_t cr_asid;

    cr_vfn = context->addr >> TARGET_PAGE_BITS;
    pte    = context->pte;

    tb->tb_tag = FIELD_DP32(tb->tb_tag, CR_TBTAG, VFN, cr_vfn);
    cr_asid = FIELD_EX32(insn ? env->CR_ITBTAG : env->CR_DTBTAG, CR_TBTAG, ASID);
    tb->tb_tag = FIELD_DP32(tb->tb_tag, CR_TBTAG, ASID, cr_asid);

    tb->tb_pte = pte;
}

static XR17032TB *xr17032_tb_search_cb(CPUXR17032State *env,
                                        vaddr vaddr, int cr_asid,
                                        tb_match func, bool insn)
{
    XR17032TB *tb;
    uint16_t tb_asid;
    bool tb_g;
    int i;
    uint64_t vpn, tb_vpn;

    vpn = (vaddr & TARGET_VIRT_MASK) >> TARGET_PAGE_BITS;

    for (i = 0; i < XR17032_TB_MAX; ++i) {
        tb = insn ? &env->itb[i] : &env->dtb[i];
        tb_asid = FIELD_EX32(tb->tb_tag, CR_TBTAG, ASID);

        if (tb_asid != CR_TBTAG_ASID_INVALID) {
            tb_vpn = FIELD_EX32(tb->tb_tag, CR_TBTAG, VFN);
            tb_g = FIELD_EX32(tb->tb_pte, CR_TBPTE, G);
            vpn = (vaddr & TARGET_VIRT_MASK) >> TARGET_PAGE_BITS;

            if (vpn == tb_vpn && func(tb_g, cr_asid, tb_asid)) {
                return tb;
            }
        }
    }
    return NULL;
}

static bool xr17032_tb_search(CPUXR17032State *env, vaddr vaddr,
                              int *index, bool insn)
{
    int cr_asid;
    tb_match func;
    XR17032TB *tb;

    func = tb_match_accessible;
    cr_asid = FIELD_EX32(insn ? env->CR_ITBTAG : env->CR_DTBTAG, CR_TBTAG, ASID);
    tb = xr17032_tb_search_cb(env, vaddr, cr_asid, func, insn);
    if (tb) {
        *index = tb - (insn ? env->itb : env->dtb);
        return true;
    }

    return false;
}

static void update_tb_index(CPUXR17032State *env, MMUContext *context,
                             int index, bool insn)
{
    XR17032TB *old, new = {};
    bool skip_inv = false, tb_v;

    old = (insn ? env->itb : env->dtb) + index;
    fill_tb_entry(env, &new, context, insn);

    if (old->tb_tag == new.tb_tag) {
        tb_v = pte_present(env, old->tb_pte);

        if (!tb_v || new.tb_pte == old->tb_pte) {
            skip_inv = true;
        }
    }

    /* flush tlb before updating the entry */
    if (!skip_inv) {
        invalidate_tb(env, index, insn);
    }

    *old = new;
}

static void write_pte(CPUXR17032State *env, target_ulong pte, bool insn)
{
    int index = insn ? env->CR_ITBINDEX : env->CR_DTBINDEX;
    MMUContext context;

    prepare_context(env, &context, insn, pte);
    update_tb_index(env, &context, index, insn);

    if (++index >= XR17032_TB_MAX) {
        index = XR17032_TB_UNWIRED;
    }

    if (insn) {
        env->CR_ITBINDEX = index;
    } else {
        env->CR_DTBINDEX = index;
    }
}

void helper_crwr_itbpte(CPUXR17032State *env, target_ulong pte)
{
    write_pte(env, pte, true);
}

void helper_crwr_dtbpte(CPUXR17032State *env, target_ulong pte)
{
    write_pte(env, pte, false);
}

static void tbctrl_vfn(CPUXR17032State *env, target_ulong vfn, bool insn)
{
    XR17032TB *tb;
    tb_match func;

    func = tb_match_any;
    tb = xr17032_tb_search_cb(env, vfn << TARGET_PAGE_BITS, 0, func, insn);

    if (tb) {
        invalidate_tb(env, tb - (insn ? env->itb : env->dtb), insn);
    }
}

static void tbctrl_private(CPUXR17032State *env, bool insn)
{
    XR17032TB *tb = insn ? env->itb : env->dtb;

    for (int i = XR17032_TB_UNWIRED; i < XR17032_TB_MAX; i++) {
        if (FIELD_EX32(tb[i].tb_pte, CR_TBPTE, G) == 0) {
            tb[i].tb_tag = FIELD_DP32(tb[i].tb_tag, CR_TBTAG, ASID,
                                      CR_TBTAG_ASID_INVALID);
        }
    }

    tlb_flush_by_mmuidx(env_cpu(env), tlb_map(insn));
}

static void tbctrl_all(CPUXR17032State *env, bool insn, int start)
{
    XR17032TB *tb = insn ? env->itb : env->dtb;

    for (int i = start; i < XR17032_TB_MAX; i++) {
        tb[i].tb_tag = FIELD_DP32(tb[i].tb_tag, CR_TBTAG, ASID,
                                  CR_TBTAG_ASID_INVALID);
    }

    tlb_flush_by_mmuidx(env_cpu(env), tlb_map(insn));
}

static void tbctrl(CPUXR17032State *env, target_ulong value, bool insn)
{
    switch (FIELD_EX32(value, CR_TBCTRL, OP)) {
    case CR_TBCTRL_OP_CLEAR_VFN:
        tbctrl_vfn(env, FIELD_EX32(value, CR_TBCTRL, VFN), insn);
        break;
    case CR_TBCTRL_OP_CLEAR_UNWIRED:
        tbctrl_all(env, insn, XR17032_TB_UNWIRED);
        break;
    case CR_TBCTRL_OP_CLEAR_PRIVATE:
        tbctrl_private(env, insn);
        break;
    case CR_TBCTRL_OP_CLEAR_FULL:
        tbctrl_all(env, insn, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

void helper_crwr_itbctrl(CPUXR17032State *env, target_ulong value)
{
    tbctrl(env, value, true);
}

void helper_crwr_dtbctrl(CPUXR17032State *env, target_ulong value)
{
    tbctrl(env, value, false);
}

bool xr17032_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                          MMUAccessType access_type, int mmu_idx,
                          bool probe, uintptr_t retaddr)
{
    CPUXR17032State *env = cpu_env(cs);
    hwaddr physical;
    int prot;
    MMUContext context;
    TLBRet ret;

    /* Data access */
    context.addr = address;
    ret = get_physical_address(env, &context, access_type, mmu_idx, 0);

    if (ret == TLBRET_MATCH) {
        physical = context.physical;
        prot = context.prot;
        tlb_set_page(cs, address & TARGET_PAGE_MASK,
                     physical & TARGET_PAGE_MASK, prot,
                     mmu_idx, TARGET_PAGE_SIZE);
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " physical " HWADDR_FMT_plx
                      " prot %d\n", __func__, address, physical, prot);
        return true;
    } else {
        qemu_log_mask(CPU_LOG_MMU,
                      "%s address=%" VADDR_PRIx " ret %d\n", __func__, address,
                      ret);
    }
    if (probe) {
        return false;
    }
    raise_mmu_exception(env, address, access_type, ret);
    cpu_loop_exit_restore(cs, retaddr);
}

static TLBRet xr17032_map_tb_entry(CPUXR17032State *env,
                                    MMUContext *context,
                                    MMUAccessType access_type, int index,
                                    int mmu_idx)
{
    XR17032TB *tb = MMU_INSN(mmu_idx) ? &env->itb[index] : &env->dtb[index];

    context->pte = tb->tb_pte;
    return xr17032_check_pte(env, context, access_type, mmu_idx);
}

TLBRet xr17032_get_addr_from_tb(CPUXR17032State *env,
                                 MMUContext *context,
                                 MMUAccessType access_type, int mmu_idx)
{
    int index, match;

    match = xr17032_tb_search(env, context->addr, &index,
                              MMU_INSN(mmu_idx));
    if (match) {
        return xr17032_map_tb_entry(env, context, access_type, index,
                                       mmu_idx);
    }

    return TLBRET_NOMATCH;
}

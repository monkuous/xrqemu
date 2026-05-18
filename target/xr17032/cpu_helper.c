/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/17032 CPU helpers for qemu
 *
 * Copyright (c) 2026 monkuous
 *
 */

#include "qemu/osdep.h"
#include "system/tcg.h"
#include "cpu.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "exec/target_page.h"
#include "internals.h"
#include "cpu-cr.h"
#include "cpu-mmu.h"
#include "tcg/tcg_xr17032.h"

TLBRet xr17032_check_pte(CPUXR17032State *env, MMUContext *context,
                         MMUAccessType access_type, int mmu_idx)
{
    uint32_t plv = MMU_PLV(mmu_idx);
    uint32_t tb_entry, tlb_ppn;
    uint8_t tb_plv;
    bool tb_v, tb_w;

    tb_entry = context->pte;
    tb_v = pte_present(env, tb_entry);
    tb_w = pte_write(env, tb_entry);
    tb_plv = !FIELD_EX32(tb_entry, CR_TBPTE, K);
    tlb_ppn = FIELD_EX32(tb_entry, CR_TBPTE, PFN);

    /* Check access rights */
    if (!tb_v) {
        return TLBRET_INVALID;
    }

    if (plv > tb_plv) {
        return TLBRET_PE;
    }

    if ((access_type == MMU_DATA_STORE) && !tb_w) {
        return TLBRET_WI;
    }

    context->physical = (tlb_ppn << TARGET_PAGE_BITS) |
                        (context->addr & MAKE_64BIT_MASK(0, TARGET_PAGE_BITS));
    context->prot = PAGE_READ | PAGE_EXEC;
    if (tb_w) {
        context->prot |= PAGE_WRITE;
    }
    return TLBRET_MATCH;
}

static TLBRet xr17032_walk_tables(CPUXR17032State *env, MMUContext *context,
                                  MMUAccessType access_type, int mmu_idx)
{
    CPUState *cs = env_cpu(env);
    MMUContext pd_context;
    TLBRet ret = xr17032_get_addr_from_tb(env, context, access_type, mmu_idx);

    if (ret != TLBRET_NOMATCH) {
        return ret;
    }

    pd_context.addr = MMU_INSN(mmu_idx) ? env->CR_ITBADDR : env->CR_DTBADDR;
    pd_context.addr = FIELD_DP32(pd_context.addr, CR_TBADDR, VFN,
        context->addr >> TARGET_PAGE_BITS);

    ret = xr17032_get_addr_from_tb(env, &pd_context, MMU_DATA_LOAD,
        MMU_KERNEL_DATA);

    if (ret != TLBRET_MATCH) {
        return ret;
    }

    context->pte = ldl_le_phys(cs->as, pd_context.physical);
    return xr17032_check_pte(env, context, access_type, mmu_idx);
}

static TLBRet xr17032_map_address(CPUXR17032State *env,
                                  MMUContext *context,
                                  MMUAccessType access_type, int mmu_idx,
                                  int is_debug)
{
    TLBRet ret;

    if (tcg_enabled()) {
        ret = xr17032_get_addr_from_tb(env, context, access_type, mmu_idx);
        if (ret != TLBRET_NOMATCH) {
            return ret;
        }
    }

    if (is_debug) {
        /* Do the page walk manually */
        return xr17032_walk_tables(env, context, access_type, mmu_idx);
    }

    return TLBRET_NOMATCH;
}

TLBRet get_physical_address(CPUXR17032State *env, MMUContext *context,
                            MMUAccessType access_type, int mmu_idx,
                            int is_debug)
{
    uint8_t mmu = FIELD_EX32(env->CR_RS, CR_RS, M);
    vaddr address;

    /* Check RS.M */
    address = context->addr;
    if (!mmu) {
        context->physical = address & UINT32_MAX;
        context->prot = PAGE_READ | PAGE_WRITE | PAGE_EXEC;
        return TLBRET_MATCH;
    }

    /* Mapped address */
    return xr17032_map_address(env, context, access_type, mmu_idx, is_debug);
}

hwaddr xr17032_cpu_get_phys_page_debug(CPUState *cs, vaddr addr)
{
    CPUXR17032State *env = cpu_env(cs);
    MMUContext context;

    context.addr = addr;
    if (get_physical_address(env, &context, MMU_DATA_LOAD,
                             cpu_mmu_index(cs, false), 1) != TLBRET_MATCH) {
        return -1;
    }
    return context.physical;
}

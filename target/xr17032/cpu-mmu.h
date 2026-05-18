/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/17032 CPU parameters for QEMU.
 *
 * Copyright (c) 2026 monkuous
 */

#ifndef XR17032_CPU_MMU_H
#define XR17032_CPU_MMU_H

typedef enum TLBRet {
    TLBRET_MATCH,
    TLBRET_NOMATCH,
    TLBRET_INVALID,
    TLBRET_WI,
    TLBRET_PE,
} TLBRet;

typedef struct MMUContext {
    vaddr         addr;
    uint32_t      pte;
    hwaddr        physical;
    int           prot;
} MMUContext;

static inline bool pte_present(CPUXR17032State *env, uint32_t entry)
{
    return !!FIELD_EX32(entry, CR_TBPTE, V);
}

static inline bool pte_write(CPUXR17032State *env, uint32_t entry)
{
    return !!FIELD_EX32(entry, CR_TBPTE, W);
}

TLBRet xr17032_check_pte(CPUXR17032State *env, MMUContext *context,
                         MMUAccessType access_type, int mmu_idx);
TLBRet get_physical_address(CPUXR17032State *env, MMUContext *context,
                            MMUAccessType access_type, int mmu_idx,
                            int is_debug);
void get_dir_base_width(CPUXR17032State *env, uint64_t *dir_base,
                        uint64_t *dir_width, unsigned int level);
hwaddr xr17032_cpu_get_phys_page_debug(CPUState *cpu, vaddr addr);

#endif  /* XR17032_CPU_MMU_H */

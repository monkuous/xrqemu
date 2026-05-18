/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU XR/17032 TCG interface
 *
 * Copyright (c) 2026 monkuous
 */
#ifndef TARGET_XR17032_TCG_XR17032_H
#define TARGET_XR17032_TCG_XR17032_H
#include "cpu.h"
#include "cpu-mmu.h"

extern const TCGCPUOps xr17032_tcg_ops;
void xr17032_cr_translate_init(void);

bool xr17032_cpu_tlb_fill(CPUState *cs, vaddr address, int size,
                          MMUAccessType access_type, int mmu_idx,
                          bool probe, uintptr_t retaddr);

TLBRet xr17032_get_addr_from_tb(CPUXR17032State *env,
                                 MMUContext *context,
                                 MMUAccessType access_type, int mmu_idx);

#endif  /* TARGET_XR17032_TCG_XR17032_H */

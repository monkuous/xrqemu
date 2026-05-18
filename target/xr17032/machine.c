/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU XR/17032 Machine State
 *
 * Copyright (c) 2026 monkuous
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "migration/cpu.h"
#include "system/tcg.h"

#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
static bool tlb_needed(void *opaque)
{
    return tcg_enabled();
}

/* TLB state */
static const VMStateDescription vmstate_tb_entry = {
    .name = "cpu/tb_entry",
    .version_id = 0,
    .minimum_version_id = 0,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(tb_tag, XR17032TB),
        VMSTATE_UINT32(tb_pte, XR17032TB),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_itb = {
    .name = "cpu/itb",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = tlb_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(env.itb, XR17032CPU, XR17032_TB_MAX,
                             0, vmstate_tb_entry, XR17032TB),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_dtb = {
    .name = "cpu/dtb",
    .version_id = 0,
    .minimum_version_id = 0,
    .needed = tlb_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(env.dtb, XR17032CPU, XR17032_TB_MAX,
                             0, vmstate_tb_entry, XR17032TB),
        VMSTATE_END_OF_LIST()
    }
};
#endif

/* XR/17032 CPU state */
const VMStateDescription vmstate_xr17032_cpu = {
    .name = "cpu",
    .version_id = 4,
    .minimum_version_id = 4,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(env.gpr, XR17032CPU, 32),
        VMSTATE_UINT32(env.pc, XR17032CPU),

        /* Remaining CRs */
        VMSTATE_UINT32(env.CR_RS, XR17032CPU),
        VMSTATE_UINT32(env.CR_WHAMI, XR17032CPU),
        VMSTATE_UINT32(env.CR_EB, XR17032CPU),
        VMSTATE_UINT32(env.CR_EPC, XR17032CPU),
        VMSTATE_UINT32(env.CR_EBADADDR, XR17032CPU),
        VMSTATE_UINT32(env.CR_TBMISSADDR, XR17032CPU),
        VMSTATE_UINT32(env.CR_TBPC, XR17032CPU),
        VMSTATE_UINT32(env.CR_SCRATCH0, XR17032CPU),
        VMSTATE_UINT32(env.CR_SCRATCH1, XR17032CPU),
        VMSTATE_UINT32(env.CR_SCRATCH2, XR17032CPU),
        VMSTATE_UINT32(env.CR_SCRATCH3, XR17032CPU),
        VMSTATE_UINT32(env.CR_SCRATCH4, XR17032CPU),
        VMSTATE_UINT32(env.CR_ITBPTE, XR17032CPU),
        VMSTATE_UINT32(env.CR_ITBTAG, XR17032CPU),
        VMSTATE_UINT32(env.CR_ITBINDEX, XR17032CPU),
        VMSTATE_UINT32(env.CR_ITBCTRL, XR17032CPU),
        VMSTATE_UINT32(env.CR_ICACHECTRL, XR17032CPU),
        VMSTATE_UINT32(env.CR_ITBADDR, XR17032CPU),
        VMSTATE_UINT32(env.CR_DTBPTE, XR17032CPU),
        VMSTATE_UINT32(env.CR_DTBTAG, XR17032CPU),
        VMSTATE_UINT32(env.CR_DTBINDEX, XR17032CPU),
        VMSTATE_UINT32(env.CR_DTBCTRL, XR17032CPU),
        VMSTATE_UINT32(env.CR_DCACHECTRL, XR17032CPU),
        VMSTATE_UINT32(env.CR_DTBADDR, XR17032CPU),

        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
#if defined(CONFIG_TCG) && !defined(CONFIG_USER_ONLY)
        &vmstate_itb,
        &vmstate_dtb,
#endif
        NULL
    }
};

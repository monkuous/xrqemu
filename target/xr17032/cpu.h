/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU XR/17032 CPU
 *
 * Copyright (c) 2026 monkuous
 */

#ifndef XR17032_CPU_H
#define XR17032_CPU_H

#include "exec/cpu-common.h"
#include "exec/cpu-defs.h"
#include "exec/cpu-interrupt.h"
#include "hw/core/registerfields.h"
#include "qemu/timer.h"
#ifndef CONFIG_USER_ONLY
#include "system/memory.h"
#endif
#include "cpu-cr.h"
#include "cpu-qom.h"

#define XR17032_LR 31

#define EXCCODE_INT 1
#define EXCCODE_SYS 2
#define EXCCODE_BUS 4
#define EXCCODE_NMI 5
#define EXCCODE_BRK 6
#define EXCCODE_INV 7
#define EXCCODE_PRV 8
#define EXCCODE_UNA 9
#define EXCCODE_PGF 12
#define EXCCODE_PFW 13
#define EXCCODE_ITB 14
#define EXCCODE_DTB 15

/*CR_RS */
FIELD(CR_RS, U, 0, 1)
FIELD(CR_RS, I, 1, 1)
FIELD(CR_RS, M, 2, 1)
FIELD(CR_RS, T, 3, 1)
FIELD(CR_RS, MODE, 0, 8)
FIELD(CR_RS, STACK, 0, 24)
FIELD(CR_RS, ECAUSE, 28, 4)

extern const char * const regnames[32];

#define XR17032_TB_UNWIRED 4
#define XR17032_TB_MAX 32

#ifdef CONFIG_TCG
struct XR17032TB {
    uint32_t tb_tag;
    uint32_t tb_pte;
};
typedef struct XR17032TB XR17032TB;
#endif

typedef struct CPUArchState {
    uint32_t gpr[32];
    uint32_t pc;

    /* XR/17032 CRs */
    uint32_t CR_RS;
    uint32_t CR_WHAMI;
    uint32_t CR_EB;
    uint32_t CR_EPC;
    uint32_t CR_EBADADDR;
    uint32_t CR_TBMISSADDR;
    uint32_t CR_TBPC;
    uint32_t CR_SCRATCH0;
    uint32_t CR_SCRATCH1;
    uint32_t CR_SCRATCH2;
    uint32_t CR_SCRATCH3;
    uint32_t CR_SCRATCH4;
    uint32_t CR_ITBPTE;
    uint32_t CR_ITBTAG;
    uint32_t CR_ITBINDEX;
    uint32_t CR_ITBCTRL;
    uint32_t CR_ICACHECTRL;
    uint32_t CR_ITBADDR;
    uint32_t CR_DTBPTE;
    uint32_t CR_DTBTAG;
    uint32_t CR_DTBINDEX;
    uint32_t CR_DTBCTRL;
    uint32_t CR_DCACHECTRL;
    uint32_t CR_DTBADDR;

    bool tb_miss_was_write;

#ifdef CONFIG_TCG
    uint32_t lladdr; /* LL virtual address compared against SC */
    uint32_t llval;
#endif
#ifndef CONFIG_USER_ONLY
#ifdef CONFIG_TCG
    XR17032TB itb[XR17032_TB_MAX];
    XR17032TB dtb[XR17032_TB_MAX];
#endif
#endif
} CPUXR17032State;

/**
 * XR17032CPU:
 * @env: #CPUXR17032State
 *
 * An XR/17032 CPU.
 */
struct ArchCPU {
    CPUState parent_obj;

    CPUXR17032State env;
    uint32_t  phy_id;
    int32_t socket_id;  /* socket-id of this CPU */
    int32_t node_id;    /* NUMA node of this CPU */
    bool pause_on_crash;

    /* 'compatible' string for this CPU for Linux device trees */
    const char *dtb_compatible;
};

/**
 * XR17032CPUClass:
 * @parent_realize: The parent class' realize handler.
 * @parent_phases: The parent class' reset phase handlers.
 *
 * An XR/17032 CPU model.
 */
struct XR17032CPUClass {
    CPUClass parent_class;

    DeviceRealize parent_realize;
    DeviceUnrealize parent_unrealize;
    ResettablePhases parent_phases;
};

/*
 * XR/17032 CPUs has 2 privilege levels.
 * 0 for kernel mode, 1 for user mode.
 * Each needs two MMUs (one for data, one for instructions).
 * Define an extra index for DA(direct addressing) mode.
 */
#define MMU_PLV_KERNEL 0
#define MMU_PLV_USER   1

#define MMU_IDX(PLV, INSN) ((PLV) * 2 + !!(INSN))
#define MMU_PLV(IDX) ((IDX) / 2)
#define MMU_INSN(IDX) (!!((IDX) & 1))
#define MMU_KERNEL_DATA MMU_IDX(MMU_PLV_KERNEL, 0)
#define MMU_KERNEL_INSN MMU_IDX(MMU_PLV_KERNEL, 1)
#define MMU_USER_DATA   MMU_IDX(MMU_PLV_USER, 0)
#define MMU_USER_INSN   MMU_IDX(MMU_PLV_USER, 1)
#define MMU_DIRECT      4

/*
 * XR/17032 CPUs hardware flags.
 */
#define HW_FLAGS_PLV_MASK   R_CR_RS_U_MASK  /* 0x01 */
#define HW_FLAGS_RS_M       R_CR_RS_M_MASK  /* 0x04 */
#define HW_FLAGS_RS_T       R_CR_RS_T_MASK  /* 0x08 */

#define CPU_RESOLVING_TYPE TYPE_XR17032_CPU

#endif /* XR17032_CPU_H */

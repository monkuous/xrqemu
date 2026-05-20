/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/17032 CPU parameters for QEMU.
 *
 * Copyright (c) 2026 monkuous
 */
#include "qemu/osdep.h"
#include "qemu/accel.h"
#include "qemu/error-report.h"
#include "qemu/log.h"
#include "qemu/plugin.h"
#include "accel/accel-cpu-target.h"
#include "accel/tcg/cpu-ldst.h"
#include "accel/tcg/cpu-ops.h"
#include "exec/translation-block.h"
#include "exec/target_page.h"
#include "tcg_xr17032.h"
#include "internals.h"
#include "system/runstate.h"

struct TypeExcp {
    int32_t exccode;
    const char * const name;
};

static const struct TypeExcp excp_names[] = {
    {EXCCODE_INT, "Interrupt"},
    {EXCCODE_SYS, "Syscall"},
    {EXCCODE_BUS, "Bus error"},
    {EXCCODE_NMI, "Non-maskable interrupt"},
    {EXCCODE_BRK, "Breakpoint"},
    {EXCCODE_INV, "Invalid instruction"},
    {EXCCODE_PRV, "Privileged instruction"},
    {EXCCODE_UNA, "Unaligned access"},
    {EXCCODE_PGF, "Read page fault"},
    {EXCCODE_PFW, "Write page fault"},
    {EXCCODE_ITB, "ITB miss"},
    {EXCCODE_DTB, "DTB miss"},
    {EXCP_HLT, "EXCP_HLT"},
};

static const char *xr17032_exception_name(int32_t exception)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(excp_names); i++) {
        if (excp_names[i].exccode == exception) {
            return excp_names[i].name;
        }
    }
    return "Unknown";
}

void G_NORETURN do_raise_exception(CPUXR17032State *env,
                                   uint32_t exception,
                                   uintptr_t pc)
{
    CPUState *cs = env_cpu(env);

    cs->exception_index = exception;

    cpu_loop_exit_restore(cs, pc);
}

#ifndef CONFIG_USER_ONLY
static target_ulong push_stack(target_ulong rs)
{
    target_ulong stack = FIELD_EX32(rs, CR_RS, STACK) << R_CR_RS_MODE_LENGTH;
    stack |= FIELD_EX32(rs, CR_RS, MODE);
    return FIELD_DP32(rs, CR_RS, STACK, stack & R_CR_RS_STACK_MASK);
}

static void xr17032_cpu_do_interrupt(CPUState *cs)
{
    CPUXR17032State *env = cpu_env(cs);
    int cause = cs->exception_index;
    bool skip_push = false;
    uint32_t last_pc = env->pc;

    if (cause != EXCCODE_INT) {
        qemu_log_mask(CPU_LOG_INT,
                     "%s enter: pc " TARGET_FMT_lx " EPC " TARGET_FMT_lx
                     " TBPC " TARGET_FMT_lx " RS " TARGET_FMT_lx
                      " EB " TARGET_FMT_lx " exception: %d (%s)\n",
                     __func__, env->pc, env->CR_EPC, env->CR_TBPC, env->CR_RS,
                     env->CR_EB, cause, xr17032_exception_name(cause));
    }

    if (env->CR_EB == 0) {
        if (XR17032_CPU(cs)->pause_on_crash) {
            vm_stop(RUN_STATE_PAUSED);
        } else {
            cpu_interrupt(cs, CPU_INTERRUPT_RESET);
        }

        return;
    }

    switch (cause) {
    case EXCCODE_SYS:
    case EXCCODE_BRK:
        last_pc += 4;
        QEMU_FALLTHROUGH;
    case EXCCODE_INT:
    case EXCCODE_BUS:
    case EXCCODE_NMI:
    case EXCCODE_INV:
    case EXCCODE_PRV:
    case EXCCODE_UNA:
        break;
    case EXCCODE_PGF:
    case EXCCODE_PFW:
        skip_push = FIELD_EX32(env->CR_RS, CR_RS, T);

        if (skip_push) {
            env->CR_EPC = env->CR_TBPC;
            env->CR_RS = FIELD_DP32(env->CR_RS, CR_RS, T, 0);
        }
        break;
    case EXCCODE_ITB:
    case EXCCODE_DTB:
        skip_push = true;

        if (!FIELD_EX32(env->CR_RS, CR_RS, T)) {
            env->CR_TBPC = env->pc;
            env->CR_RS = push_stack(env->CR_RS);
            env->CR_RS = FIELD_DP32(env->CR_RS, CR_RS, T, 1);
        }
        break;
    default:
        qemu_log("Error: exception(%d) has not been supported\n", cause);
        abort();
    }

    if (!skip_push) {
        env->CR_EPC = last_pc;
        env->CR_RS = push_stack(env->CR_RS);
    }

    if (cause != EXCCODE_ITB && cause != EXCCODE_DTB) {
        env->CR_RS = FIELD_DP32(env->CR_RS, CR_RS, ECAUSE, cause);
    }

    env->CR_RS = FIELD_DP32(env->CR_RS, CR_RS, U, 0);
    env->CR_RS = FIELD_DP32(env->CR_RS, CR_RS, I, 0);

    env->pc = env->CR_EB | (cause << 8);

    qemu_log_mask(CPU_LOG_INT,
                     "%s: pc " TARGET_FMT_lx " EPC " TARGET_FMT_lx
                     " TBPC " TARGET_FMT_lx " RS " TARGET_FMT_lx " cause %d\n",
                     __func__, env->pc, env->CR_EPC, env->CR_TBPC, env->CR_RS,
                     cause);

    if (cause == EXCCODE_INT) {
        qemu_plugin_vcpu_interrupt_cb(cs, last_pc);
    } else {
        qemu_plugin_vcpu_exception_cb(cs, last_pc);
    }

    cs->exception_index = -1;
}

static void xr17032_cpu_do_transaction_failed(CPUState *cs, hwaddr physaddr,
                                              vaddr addr, unsigned size,
                                              MMUAccessType access_type,
                                              int mmu_idx, MemTxAttrs attrs,
                                              MemTxResult response,
                                              uintptr_t retaddr)
{
    CPUXR17032State *env = cpu_env(cs);
    env->CR_EBADADDR = physaddr;
    do_raise_exception(env, EXCCODE_BUS, retaddr);
}

static inline bool cpu_xr17032_hw_interrupts_enabled(CPUXR17032State *env)
{
    return !!FIELD_EX32(env->CR_RS, CR_RS, I);
}

static bool xr17032_cpu_exec_interrupt(CPUState *cs, int interrupt_request)
{
    if (interrupt_request & CPU_INTERRUPT_HARD) {
        CPUXR17032State *env = cpu_env(cs);

        if (cpu_xr17032_hw_interrupts_enabled(env)) {
            /* Raise it */
            cs->exception_index = EXCCODE_INT;
            xr17032_cpu_do_interrupt(cs);
            return true;
        }
    }

    return false;
}

static vaddr xr17032_pointer_wrap(CPUState *cs, int mmu_idx,
                                    vaddr result, vaddr base)
{
    return result;
}
#endif

static TCGTBCPUState xr17032_get_tb_cpu_state(CPUState *cs)
{
    CPUXR17032State *env = cpu_env(cs);
    uint32_t flags;

    flags = env->CR_RS & (R_CR_RS_U_MASK | R_CR_RS_M_MASK | R_CR_RS_T_MASK);

    return (TCGTBCPUState){ .pc = env->pc, .flags = flags };
}

static void xr17032_cpu_synchronize_from_tb(CPUState *cs,
                                            const TranslationBlock *tb)
{
    tcg_debug_assert(!tcg_cflags_has(cs, CF_PCREL));
    cpu_env(cs)->pc = tb->pc;
}

static void xr17032_restore_state_to_opc(CPUState *cs,
                                         const TranslationBlock *tb,
                                         const uint64_t *data)
{
    cpu_env(cs)->pc = data[0];
}

static int xr17032_cpu_mmu_index(CPUState *cs, bool ifetch)
{
    CPUXR17032State *env = cpu_env(cs);

    if (FIELD_EX32(env->CR_RS, CR_RS, M)) {
        return MMU_IDX(FIELD_EX32(env->CR_RS, CR_RS, U), ifetch);
    }

    return MMU_DIRECT;
}

const TCGCPUOps xr17032_tcg_ops = {
    .guest_default_memory_order = 0,
    .mttcg_supported = true,

    .initialize = xr17032_translate_init,
    .translate_code = xr17032_translate_code,
    .get_tb_cpu_state = xr17032_get_tb_cpu_state,
    .synchronize_from_tb = xr17032_cpu_synchronize_from_tb,
    .restore_state_to_opc = xr17032_restore_state_to_opc,
    .mmu_index = xr17032_cpu_mmu_index,

#ifndef CONFIG_USER_ONLY
    .tlb_fill = xr17032_cpu_tlb_fill,
    .pointer_wrap = xr17032_pointer_wrap,
    .cpu_exec_interrupt = xr17032_cpu_exec_interrupt,
    .cpu_exec_halt = xr17032_cpu_has_work,
    .cpu_exec_reset = cpu_reset,
    .do_interrupt = xr17032_cpu_do_interrupt,
    .do_transaction_failed = xr17032_cpu_do_transaction_failed,
#endif
};

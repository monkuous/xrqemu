/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/17032 emulation helpers for QEMU.
 *
 * Copyright (c) 2026 monkuous
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "cpu.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "accel/tcg/cpu-ldst.h"
#include "internals.h"
#include "cpu-cr.h"

/* Exceptions helpers */
void helper_raise_exception(CPUXR17032State *env, uint32_t exception)
{
    do_raise_exception(env, exception, GETPC());
}

#ifndef CONFIG_USER_ONLY
void helper_rfe(CPUXR17032State *env)
{
    env->lladdr = 1;

    if (FIELD_EX32(env->CR_RS, CR_RS, T)) {
        env->pc = env->CR_TBPC;
    } else {
        env->pc = env->CR_EPC;
    }

    env->CR_RS = FIELD_DP32(env->CR_RS, CR_RS, STACK,
        FIELD_EX32(env->CR_RS, CR_RS, STACK) >> R_CR_RS_MODE_LENGTH);
}

void helper_hlt(CPUXR17032State *env)
{
    CPUState *cs = env_cpu(env);

    cs->halted = 1;
    do_raise_exception(env, EXCP_HLT, 0);
}
#endif

/*
 * XR17032 gdb server stub
 *
 * Copyright (c) 2026 monkuous
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "internals.h"
#include "exec/gdbstub.h"
#include "gdbstub/helpers.h"

int xr17032_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n)
{
    CPUXR17032State *env = cpu_env(cs);

    if (0 <= n && n <= 32) {
        uint64_t val;

        if (n < 32) {
            val = env->gpr[n];
        } if (n == 32) {
            val = env->pc;
        }

        return gdb_get_reg32(mem_buf, val);
    }

    return 0;
}

int xr17032_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n)
{
    CPUXR17032State *env = cpu_env(cs);
    uint64_t tmp;
    int length = 0;

    if (n < 0 || n > 32) {
        return 0;
    }

    tmp = ldl_le_p(mem_buf);
    length = 4;

    if (0 <= n && n < 32) {
        env->gpr[n] = tmp;
    } else if (n == 32) {
        env->pc = tmp;
    }
    return length;
}

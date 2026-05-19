/*
 * QEMU monitor for XR/17032
 *
 * Copyright (c) 2026 monkuous
 *
 * XR/17032 specific monitor commands implementation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "qemu/ctype.h"
#include "qemu/qemu-print.h"
#include "cpu.h"
#include "monitor/monitor.h"
#include "monitor/hmp.h"
#include "monitor/hmp-target.h"
#include "system/memory.h"

static void dump_tb(Monitor *mon, XR17032TB *tb, const char *name)
{
    int i;

    for (i = 0; i < XR17032_TB_MAX; i++, tb++) {
        if (FIELD_EX32(tb->tb_tag, CR_TBTAG, ASID) == CR_TBTAG_ASID_INVALID) {
            continue;
        }

        monitor_printf(mon, "%s[%.4d]:  %.3x[%.5x] -> %.5x %c%c%c%c%c (%.8x)\n",
            name, i, FIELD_EX32(tb->tb_tag, CR_TBTAG, ASID),
            FIELD_EX32(tb->tb_tag, CR_TBTAG, VFN),
            FIELD_EX32(tb->tb_pte, CR_TBPTE, PFN),
            FIELD_EX32(tb->tb_pte, CR_TBPTE, G) ? 'G' : ' ',
            FIELD_EX32(tb->tb_pte, CR_TBPTE, N) ? 'N' : ' ',
            FIELD_EX32(tb->tb_pte, CR_TBPTE, K) ? 'K' : ' ',
            FIELD_EX32(tb->tb_pte, CR_TBPTE, W) ? 'W' : ' ',
            FIELD_EX32(tb->tb_pte, CR_TBPTE, V) ? 'V' : ' ', tb->tb_pte);
    }
}

void hmp_info_tlb(Monitor *mon, const QDict *qdict)
{
    CPUArchState *env;

    env = mon_get_cpu_env(mon);
    if (!env) {
        monitor_printf(mon, "No CPU available\n");
        return;
    }

    if (!FIELD_EX32(env->CR_RS, CR_RS, M)) {
        monitor_printf(mon, "MMU disabled\n");
        return;
    }

    monitor_printf(mon, "xTB[ IDX]: ASID[  VFN] -> PFN FLAGS (RAW)\n");
    dump_tb(mon, env->itb, "ITB");
    dump_tb(mon, env->dtb, "DTB");
}

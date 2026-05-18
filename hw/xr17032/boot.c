/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/17032 boot helper functions.
 *
 * Copyright (c) 2026 monkuous
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "target/xr17032/cpu.h"
#include "hw/xr17032/virt.h"
#include "hw/core/loader.h"
#include "elf.h"
#include "qemu/error-report.h"
#include "system/reset.h"
#include "system/qtest.h"

void xr17032_load_kernel(MachineState *ms)
{
    XR17032VirtState *s = XR17032_VIRT_MACHINE(ms);

    if (ms->kernel_filename) {
        /*
         * Expose the kernel, the command line, and the initrd in fw_cfg.
         * We don't process them here at all, it's all left to the
         * firmware.
         */
        load_image_to_fw_cfg(s->fw_cfg,
                             FW_CFG_KERNEL_SIZE, FW_CFG_KERNEL_DATA,
                             ms->kernel_filename,
                             false);
    }

    if (ms->initrd_filename) {
        load_image_to_fw_cfg(s->fw_cfg,
                             FW_CFG_INITRD_SIZE, FW_CFG_INITRD_DATA,
                             ms->initrd_filename, false);
    }

    if (ms->kernel_cmdline) {
        fw_cfg_add_i32(s->fw_cfg, FW_CFG_CMDLINE_SIZE,
                       strlen(ms->kernel_cmdline) + 1);
        fw_cfg_add_string(s->fw_cfg, FW_CFG_CMDLINE_DATA,
                          ms->kernel_cmdline);
    }
}

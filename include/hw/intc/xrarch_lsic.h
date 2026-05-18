/*
 * XR/arch LSIC (Local Symmetric Interrupt Controller) interface
 *
 * Copyright (c) 2026 monkuous
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

#ifndef HW_XRARCH_LSIC_H
#define HW_XRARCH_LSIC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_XRARCH_LSIC "xrarch.lsic"

typedef struct XRarchLSICState XRarchLSICState;
DECLARE_INSTANCE_CHECKER(XRarchLSICState, XRARCH_LSIC,
                         TYPE_XRARCH_LSIC)

#define XRARCH_LSIC_STRIDE 32

struct XRarchLSICState {
    /*< private >*/
    SysBusDevice parent_obj;

    /*< public >*/
    MemoryRegion mmio;
    uint32_t num_targets;
    uint32_t num_masks;
    uint32_t *priorities;
    uint32_t *pending;
    uint32_t *disable;

    /* config */
    qemu_irq *targets;
};

DeviceState *xrarch_lsic_create(hwaddr addr, uint32_t num_targets);

#endif

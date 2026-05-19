/*
 * QEMU XR/computer machine interface
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

#ifndef HW_XR17032_XRCOMPUTER_H
#define HW_XR17032_XRCOMPUTER_H

#include "hw/core/boards.h"
#include "hw/block/flash.h"

#define XRCOMPUTER_EBUS_COUNT 7

#define TYPE_XRCOMPUTER_MACHINE MACHINE_TYPE_NAME("xrcomputer")
typedef struct XRcomputerState XRcomputerState;
DECLARE_INSTANCE_CHECKER(XRcomputerState, XRCOMPUTER_MACHINE,
                         TYPE_XRCOMPUTER_MACHINE)

struct XRcomputerState {
    /*< private >*/
    MachineState parent;
    PFlashCFI01 *nvram;
    MemoryRegion fw_rom;
    MemoryRegion reset_mmio;
    MemoryRegion board_mmio;
    uint32_t revision_data[32];

    DeviceState *irqchip;
    DeviceState *ebus[XRCOMPUTER_EBUS_COUNT];

    bool headless;

    /*< public >*/
    Notifier machine_done;
};

#endif

/*
 * QEMU XR/17032 VirtIO machine interface
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

#ifndef HW_XR17032_VIRT_H
#define HW_XR17032_VIRT_H

#include "hw/core/boards.h"
#include "hw/core/sysbus.h"
#include "hw/block/flash.h"
#include "target/xr17032/cpu.h"
#include "boot.h"

#define VIRT_CPUS_MAX 256

#define TYPE_XR17032_VIRT_MACHINE MACHINE_TYPE_NAME("virt")
typedef struct XR17032VirtState XR17032VirtState;
DECLARE_INSTANCE_CHECKER(XR17032VirtState, XR17032_VIRT_MACHINE,
                         TYPE_XR17032_VIRT_MACHINE)

struct XR17032VirtState {
    /*< private >*/
    MachineState parent;

    bool bios_loaded;

    /*< public >*/
    Notifier machine_done;
    XR17032CPU *cpus;
    DeviceState *platform_bus_dev;
    DeviceState *irqchip;
    PFlashCFI01 *flash[2];
    FWCfgState *fw_cfg;

    int fdt_size;
    const MemMapEntry *memmap;
    struct GPEXHost *gpex_host;
};

enum {
    VIRT_DRAM,
    VIRT_PCIE_MMIO,
    VIRT_PCIE_ECAM,
    VIRT_LSIC,
    VIRT_PLATFORM_BUS,
    VIRT_PCIE_PIO,
    VIRT_VIRTIO,
    VIRT_UART0,
    VIRT_FW_CFG,
    VIRT_RTC,
    VIRT_FLASH,
};

enum {
    RTC_IRQ = 2,
    UART0_IRQ = 3,
    VIRTIO_IRQ = 4, /* 4 to 11 */
    VIRTIO_COUNT = 8,
    PCIE_IRQ = 12, /* 12 to 16 */
    VIRT_PLATFORM_BUS_IRQ = 32, /* 32 to 63 */
};

#define VIRT_PLATFORM_BUS_NUM_IRQS 32

#define VIRT_IRQCHIP_NUM_SOURCES 64

#define FDT_PCI_ADDR_CELLS    3
#define FDT_PCI_INT_CELLS     1
#define FDT_INT_CELLS         1
#define FDT_INT_MAP_WIDTH (FDT_PCI_ADDR_CELLS + FDT_PCI_INT_CELLS + \
                                 1 + FDT_INT_CELLS)

#endif

/*
 * QEMU XR/arch disk controller emulator
 *
 * Copyright (c) 2026 monkuous
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef HW_BLOCK_XRARCH_DISK_H
#define HW_BLOCK_XRARCH_DISK_H

#include "hw/block/block.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define XRARCH_MAX_DISK 8

#define XRARCH_SECTOR_SIZE 512
#define XRARCH_MAX_TRANSFER 8

struct XRarchDiskCtrl;

#define TYPE_XRARCH_DISK "xrarch.disk"
OBJECT_DECLARE_SIMPLE_TYPE(XRDisk, XRARCH_DISK)

struct XRDisk {
    DeviceState qdev;
    int32_t     unit;
    BlockConf   conf;
};

#define TYPE_XRARCH_DISK_BUS "xrarch.disk-bus"
OBJECT_DECLARE_SIMPLE_TYPE(XRarchDiskBus, XRARCH_DISK_BUS)

struct XRarchDiskBus {
    BusState bus;
    struct XRarchDiskCtrl *ctrl;
};

typedef struct XRDrive {
    struct XRarchDiskCtrl *ctrl;
    BlockBackend *blk;
    BlockConf *conf;
    uint32_t sectors;
} XRDrive;

struct XRarchDiskCtrl {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    XRDrive drives[XRARCH_MAX_DISK];
    XRarchDiskBus bus;

    int32_t drive;
    uint32_t dataA;
    uint32_t dataB;
    uint32_t completion_mask;
    uint32_t next_length;
    uint32_t next_address;
    bool irqs_enabled;

    uint8_t buf[XRARCH_MAX_TRANSFER * XRARCH_SECTOR_SIZE];
};

#define TYPE_XRARCH_DISK_CTRL "xrarch.disk-ctrl"
OBJECT_DECLARE_SIMPLE_TYPE(XRarchDiskCtrl, XRARCH_DISK_CTRL)

#endif

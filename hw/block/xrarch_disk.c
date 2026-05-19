/*
 * QEMU XR/arch disk controller emulator
 *
 * Copyright (c) 2026 monkuous
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Only the basic support: it allows to switch from IWM (Integrated WOZ
 * Machine) mode to the SWIM mode and makes the linux driver happy.
 */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "qemu/main-loop.h"
#include "qapi/error.h"
#include "system/block-backend.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "hw/block/block.h"
#include "hw/block/xrarch_disk.h"
#include "hw/core/qdev-properties.h"
#include "exec/cpu-common.h"
#include "hw/core/irq.h"

#define REG_CMD 0x0
#define REG_A 0x4
#define REG_B 0x8

#define CMD_SELECT 1
#define CMD_READ 2
#define CMD_WRITE 3
#define CMD_GET_COMPLETE_MASK 4
#define CMD_GET_INFO 5
#define CMD_ENABLE_IRQ 6
#define CMD_DISABLE_IRQ 7
#define CMD_SET_LENGTH 8
#define CMD_SET_DEST 9

static const BlockDevOps xrarch_disk_block_ops = {
};

static const Property xrarch_disk_properties[] = {
    DEFINE_PROP_INT32("unit", XRDisk, unit, -1),
    DEFINE_BLOCK_PROPERTIES(XRDisk, conf),
};

static void xrarch_disk_realize(DeviceState *qdev, Error **errp)
{
    XRDisk *dev = XRARCH_DISK(qdev);
    XRarchDiskBus *bus = XRARCH_DISK_BUS(qdev->parent_bus);
    XRDrive *drive;
    uint64_t sectors;
    int ret;

    if (dev->unit == -1) {
        for (dev->unit = 0; dev->unit < XRARCH_MAX_DISK; dev->unit++) {
            drive = &bus->ctrl->drives[dev->unit];
            if (!drive->blk) {
                break;
            }
        }
    }

    if (dev->unit >= XRARCH_MAX_DISK) {
        error_setg(errp, "Can't create disk unit %d, bus supports "
                   "only %d units", dev->unit, XRARCH_MAX_DISK);
        return;
    }

    drive = &bus->ctrl->drives[dev->unit];
    if (drive->blk) {
        error_setg(errp, "Disk unit %d is in use", dev->unit);
        return;
    }

    if (!dev->conf.blk) {
        /* Anonymous BlockBackend for an empty drive */
        dev->conf.blk = blk_new(qemu_get_aio_context(), 0, BLK_PERM_ALL);
        ret = blk_attach_dev(dev->conf.blk, qdev);
        assert(ret == 0);
    }

    if (!blkconf_blocksizes(&dev->conf, errp)) {
        return;
    }

    if (dev->conf.logical_block_size != 512 ||
        dev->conf.physical_block_size != 512)
    {
        error_setg(errp, "Physical and logical block size must "
                   "be 512 for XRarch disks");
        return;
    }

    blk_get_geometry(dev->conf.blk, &sectors);

    if (sectors > UINT32_MAX) {
        sectors = UINT32_MAX;
        qemu_printf("XR/arch disks can only access 2^32 sectors, "
            "truncating accesses\n");
    }

    /*
     * rerror/werror aren't supported and therefore not even registered
     * with qdev. So set the defaults manually before they are used in
     * blkconf_apply_backend_options().
     */
    dev->conf.rerror = BLOCKDEV_ON_ERROR_AUTO;
    dev->conf.werror = BLOCKDEV_ON_ERROR_AUTO;

    if (!blkconf_apply_backend_options(&dev->conf,
                                       !blk_supports_write_perm(dev->conf.blk),
                                       false, errp)) {
        return;
    }

    /*
     * 'enospc' is the default for -drive, 'report' is what blk_new() gives us
     * for empty drives.
     */
    if (blk_get_on_error(dev->conf.blk, 0) != BLOCKDEV_ON_ERROR_ENOSPC &&
        blk_get_on_error(dev->conf.blk, 0) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "XRarch disk doesn't support drive option werror");
        return;
    }
    if (blk_get_on_error(dev->conf.blk, 1) != BLOCKDEV_ON_ERROR_REPORT) {
        error_setg(errp, "XRarch disk fdc doesn't support drive option rerror");
        return;
    }

    drive->conf = &dev->conf;
    drive->blk = dev->conf.blk;
    drive->ctrl = bus->ctrl;
    drive->sectors = sectors;

    blk_set_dev_ops(drive->blk, &xrarch_disk_block_ops, drive);
}

static void xrarch_disk_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *k = DEVICE_CLASS(klass);
    k->realize = xrarch_disk_realize;
    set_bit(DEVICE_CATEGORY_STORAGE, k->categories);
    k->bus_type = TYPE_XRARCH_DISK_BUS;
    device_class_set_props(k, xrarch_disk_properties);
    k->desc = "virtual XR/arch disk";
}

static const TypeInfo xrarch_disk_info = {
    .name = TYPE_XRARCH_DISK,
    .parent = TYPE_DEVICE,
    .class_init = xrarch_disk_class_init,
    .instance_size = sizeof(XRDisk),
};

static const TypeInfo xrarch_disk_bus_info = {
    .name = TYPE_XRARCH_DISK_BUS,
    .parent = TYPE_BUS,
    .instance_size = sizeof(XRarchDiskBus),
};

static MemTxResult xrarch_disk_ctrl_read(void *opaque, hwaddr addr,
    uint64_t *value, unsigned size, MemTxAttrs attrs)
{
    XRarchDiskCtrl *d = opaque;

    switch (addr) {
    case REG_CMD:
        *value = 0;
        return MEMTX_OK;
    case REG_A:
        *value = d->dataA;
        return MEMTX_OK;
    case REG_B:
        *value = d->dataB;
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static MemTxResult xrarch_disk_transfer(XRarchDiskCtrl *d, bool write) {
    XRDrive *drive;
    uint64_t offset;
    uint64_t length;

    if (d->drive < 0) {
        return MEMTX_ERROR;
    }

    drive = &d->drives[d->drive];

    if (!drive->blk) {
        return MEMTX_ERROR;
    }

    if (d->dataA + d->next_length < d->dataA) {
        return MEMTX_ERROR;
    }

    if (d->dataA + d->next_length > drive->sectors) {
        return MEMTX_ERROR;
    }

    offset = (uint64_t)d->dataA * XRARCH_SECTOR_SIZE;
    length = (uint64_t)d->next_length * XRARCH_SECTOR_SIZE;

    if (write) {
        cpu_physical_memory_read(d->next_address, d->buf, length);

        if (blk_pwrite(drive->blk, offset, length, d->buf, 0)) {
            qemu_printf("XR/arch disk: I/O error while writing %u sectors "
                "at %u\n", d->next_length, d->dataA);
            return MEMTX_ERROR;
        }
    } else {
        if (blk_pread(drive->blk, offset, length, d->buf, 0)) {
            qemu_printf("XR/arch disk: I/O error while reading %u sectors "
                "at %u\n", d->next_length, d->dataA);
            return MEMTX_ERROR;
        }

        cpu_physical_memory_write(d->next_address, d->buf, length);
    }

    qatomic_or(&d->completion_mask, 1 << d->drive);

    if (d->irqs_enabled) {
        qemu_irq_raise(d->irq);
    }

    return MEMTX_OK;
}

static MemTxResult xrarch_disk_ctrl_write(void *opaque, hwaddr addr,
    uint64_t value, unsigned size, MemTxAttrs attrs)
{
    XRarchDiskCtrl *d = opaque;

    switch (addr) {
    case REG_CMD:
        switch (value) {
        case CMD_SELECT:
            if (d->dataA < XRARCH_MAX_DISK && d->drives[d->dataA].blk) {
                d->drive = d->dataA;
            } else {
                d->drive = -1;
            }

            return MEMTX_OK;
        case CMD_READ:
            return xrarch_disk_transfer(d, false);
        case CMD_WRITE:
            return xrarch_disk_transfer(d, true);
        case CMD_GET_COMPLETE_MASK:
            d->dataA = 0;
            d->dataB = qatomic_xchg(&d->completion_mask, 0);
            return MEMTX_OK;
        case CMD_GET_INFO:
            if (d->dataA < XRARCH_MAX_DISK && d->drives[d->dataA].blk) {
                d->dataB = d->drives[d->dataA].sectors;
                d->dataA = 1;
            } else {
                d->dataA = 0;
                d->dataB = 0;
            }

            return MEMTX_OK;
        case CMD_ENABLE_IRQ:
            d->irqs_enabled = true;
            return MEMTX_OK;
        case CMD_DISABLE_IRQ:
            d->irqs_enabled = false;
            return MEMTX_OK;
        case CMD_SET_LENGTH:
            if (d->dataA == 0 || d->dataA > XRARCH_MAX_TRANSFER) {
                return MEMTX_ERROR;
            }

            d->next_length = d->dataA;
            return MEMTX_OK;
        case CMD_SET_DEST:
            d->next_address = d->dataA & ~511;
            return MEMTX_OK;
        }

        break;
    case REG_A:
        d->dataA = value;
        return MEMTX_OK;
    case REG_B:
        d->dataB = value;
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static const MemoryRegionOps xrarch_disk_ctrl_ops = {
    .read_with_attrs = xrarch_disk_ctrl_read,
    .write_with_attrs = xrarch_disk_ctrl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.max_access_size = 4,
    .impl.max_access_size = 4,
    .impl.min_access_size = 1,
};

static void xrarch_disk_ctrl_reset(DeviceState *d)
{
    XRarchDiskCtrl *ctrl = XRARCH_DISK_CTRL(d);

    ctrl->drive = 0;
    ctrl->dataA = 0;
    ctrl->dataB = 0;
}

static void xrarch_disk_ctrl_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    XRarchDiskCtrl *d = XRARCH_DISK_CTRL(obj);

    d->drive = -1;

    sysbus_init_mmio(sbd, &d->mmio);
    sysbus_init_irq(sbd, &d->irq);
}

static void xrarch_disk_ctrl_realize(DeviceState *dev, Error **errp)
{
    XRarchDiskCtrl *d = XRARCH_DISK_CTRL(dev);

    memory_region_init_io(&d->mmio, OBJECT(d), &xrarch_disk_ctrl_ops, d,
        "xrarch.disk", 12);

    qbus_init(&d->bus, sizeof(XRarchDiskBus), TYPE_XRARCH_DISK_BUS, dev, NULL);
    d->bus.ctrl = d;
}

static const VMStateDescription vmstate_xrdrive = {
    .name = "xrdrive",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(sectors, XRDrive),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_xrarch_disk_ctrl = {
    .name = "xrarch_disk_ctrl",
    .version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT_ARRAY(drives, XRarchDiskCtrl, XRARCH_MAX_DISK, 1,
                             vmstate_xrdrive, XRDrive),
        VMSTATE_INT32(drive, XRarchDiskCtrl),
        VMSTATE_UINT32(dataA, XRarchDiskCtrl),
        VMSTATE_UINT32(dataB, XRarchDiskCtrl),
        VMSTATE_UINT32(completion_mask, XRarchDiskCtrl),
        VMSTATE_UINT32(next_length, XRarchDiskCtrl),
        VMSTATE_UINT32(next_address, XRarchDiskCtrl),
        VMSTATE_BOOL(irqs_enabled, XRarchDiskCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static void xrarch_disk_ctrl_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = xrarch_disk_ctrl_realize;
    device_class_set_legacy_reset(dc, xrarch_disk_ctrl_reset);
    dc->vmsd = &vmstate_xrarch_disk_ctrl;
}

static const TypeInfo xrarch_disk_ctrl_info = {
    .name = TYPE_XRARCH_DISK_CTRL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = xrarch_disk_ctrl_class_init,
    .instance_init = xrarch_disk_ctrl_init,
    .instance_size = sizeof(XRarchDiskCtrl),
};

static void xrarch_disk_register_types(void)
{
    type_register_static(&xrarch_disk_ctrl_info);
    type_register_static(&xrarch_disk_bus_info);
    type_register_static(&xrarch_disk_info);
}

type_init(xrarch_disk_register_types)

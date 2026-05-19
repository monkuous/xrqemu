/*
 * QEMU Amtsu keyboard/mouse emulation
 *
 * Copyright (c) 2026 monkuous
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/sysbus.h"
#include "hw/input/amtsu.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "hw/core/qdev-properties.h"

static void amtsu_device_reset(DeviceState *dev)
{
    AmtsuDevice *d = AMTSU_DEVICE(dev);
    AmtsuDeviceClass *dc = AMTSU_DEVICE_GET_CLASS(d);

    d->irqs_enabled = false;

    if (dc->reset) {
        dc->reset(d);
    }
}

static void amtsu_device_realize(DeviceState *dev, Error **errp)
{
    AmtsuDevice *d = AMTSU_DEVICE(dev);
    AmtsuDeviceClass *dc = AMTSU_DEVICE_GET_CLASS(d);
    AmtsuBus *bus = AMTSU_BUS(qdev_get_parent_bus(dev));
    Error *local_err = NULL;

    if (d->id < 1 || d->id >= NUM_AMTSU_DEVICES) {
        error_setg(errp, "Amtsu: invalid ID %d for %s", d->id,
            object_get_typename(OBJECT(d)));
        return;
    } else if (bus->devices[d->id]) {
        error_setg(errp, "Amtsu: ID %d not available for %s, in use by %s",
            d->id, object_get_typename(OBJECT(d)),
            object_get_typename(OBJECT(bus->devices[d->id])));
        return;
    }

    bus->devices[d->id] = d;

    if (dc->realize) {
        dc->realize(d, &local_err);

        if (local_err) {
            error_propagate(errp, local_err);
            bus->devices[d->id] = NULL;
        }
    }
}

static void amtsu_device_unrealize(DeviceState *dev)
{
    AmtsuDevice *d = AMTSU_DEVICE(dev);
    AmtsuDeviceClass *dc = AMTSU_DEVICE_GET_CLASS(d);
    AmtsuBus *bus = AMTSU_BUS(qdev_get_parent_bus(dev));

    if (dc->unrealize) {
        dc->unrealize(d);
    }

    bus->devices[d->id] = NULL;
}

static const Property amtsu_device_props[] = {
    DEFINE_PROP_INT32("addr", AmtsuDevice, id, 1),
};

const VMStateDescription vmstate_amtsu_device = {
    .name = "amtsu_device",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(irqs_enabled, AmtsuDevice),
        VMSTATE_UINT32(dataA, AmtsuDevice),
        VMSTATE_UINT32(dataB, AmtsuDevice),
        VMSTATE_END_OF_LIST(),
    },
};

void amtsu_raise_irq(AmtsuDevice *dev)
{
    AmtsuBus *bus = AMTSU_BUS(qdev_get_parent_bus(DEVICE(dev)));

    if (dev->irqs_enabled) {
        qemu_irq_raise(bus->irq[dev->id - 1]);
    }
}

void amtsu_enable_irq(AmtsuDevice *dev)
{
    dev->irqs_enabled = true;
}

void amtsu_disable_irq(AmtsuDevice *dev)
{
    dev->irqs_enabled = false;
}

static void amtsu_device_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_props(dc, amtsu_device_props);
    device_class_set_legacy_reset(dc, amtsu_device_reset);
    dc->bus_type = TYPE_AMTSU_BUS;
    dc->realize = amtsu_device_realize;
    dc->unrealize = amtsu_device_unrealize;
    dc->vmsd = &vmstate_amtsu_device;
}

static const TypeInfo amtsu_device_info = {
    .name = TYPE_AMTSU_DEVICE,
    .parent = TYPE_DEVICE,
    .abstract = true,
    .class_init = amtsu_device_class_init,
    .class_size = sizeof(AmtsuDeviceClass),
    .instance_size = sizeof(AmtsuDevice),
};

static char *amtsu_get_fw_dev_path(DeviceState *dev)
{
    AmtsuDevice *d = AMTSU_DEVICE(dev);

    return g_strdup_printf("%s@%d", qdev_fw_name(dev), d->id);
}

static void amtsu_bus_class_init(ObjectClass *klass, const void *data)
{
    BusClass *k = BUS_CLASS(klass);

    k->get_fw_dev_path = amtsu_get_fw_dev_path;
}

static const TypeInfo amtsu_bus_info = {
    .name = TYPE_AMTSU_BUS,
    .parent = TYPE_BUS,
    .class_init = amtsu_bus_class_init,
    .instance_size = sizeof(AmtsuBus),
};

#define REG_SELECT 0x00
#define REG_MID    0x04
#define REG_CMD    0x08
#define REG_A      0x0c
#define REG_B      0x10

#define CMD_IRQ_ENABLE  1
#define CMD_RESET       2
#define CMD_IRQ_DISABLE 3

static MemTxResult amtsu_bridge_read(void *opaque, hwaddr addr, uint64_t *value,
    unsigned size, MemTxAttrs attrs)
{
    AmtsuBridge *bridge = opaque;
    AmtsuDevice *d;
    AmtsuDeviceClass *dc;
    MemTxResult result;
    uint32_t val;

    switch (addr) {
    case REG_SELECT:
        *value = bridge->device;
        return MEMTX_OK;
    case REG_MID:
        if (bridge->device == 0) {
            *value = 0;
            return MEMTX_OK;
        }

        d = bridge->bus->devices[bridge->device];

        if (d == NULL) {
            *value = 0;
            return MEMTX_OK;
        }

        *value = d->model;
        return MEMTX_OK;
    case REG_CMD:
        if (bridge->device == 0) {
            *value = 0;
            return MEMTX_OK;
        }

        d = bridge->bus->devices[bridge->device];

        if (d == NULL) {
            return MEMTX_ERROR;
        }

        dc = AMTSU_DEVICE_GET_CLASS(d);

        if (dc->cmd_read) {
            result = dc->cmd_read(d, &val);
            *value = val;
            return result;
        }

        *value = 0;
        return MEMTX_OK;
    case REG_A:
        if (bridge->device == 0) {
            *value = bridge->dataA;
            return MEMTX_OK;
        }

        d = bridge->bus->devices[bridge->device];

        if (d == NULL) {
            return MEMTX_ERROR;
        }

        *value = d->dataA;
        return MEMTX_OK;
    case REG_B:
        if (bridge->device == 0) {
            *value = bridge->dataB;
            return MEMTX_OK;
        }

        d = bridge->bus->devices[bridge->device];

        if (d == NULL) {
            return MEMTX_ERROR;
        }

        *value = d->dataB;
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static MemTxResult amtsu_bridge_command(AmtsuBridge *bridge, uint32_t cmd)
{
    AmtsuDevice *d;

    switch (cmd) {
    case CMD_IRQ_ENABLE:
        if (bridge->dataB < 1 || bridge->dataB >= NUM_AMTSU_DEVICES) {
            break;
        }

        d = bridge->bus->devices[bridge->dataB];

        if (d != NULL) {
            amtsu_enable_irq(d);
        }

        break;
    case CMD_RESET:
        bus_cold_reset(BUS(bridge->bus));
        break;
    case CMD_IRQ_DISABLE:
        if (bridge->dataB < 1 || bridge->dataB >= NUM_AMTSU_DEVICES) {
            break;
        }

        d = bridge->bus->devices[bridge->dataB];

        if (d != NULL) {
            amtsu_disable_irq(d);
        }

        break;
    }

    return MEMTX_OK;
}

static MemTxResult amtsu_bridge_write(void *opaque, hwaddr addr, uint64_t value,
    unsigned size, MemTxAttrs attrs)
{
    AmtsuBridge *bridge = opaque;
    AmtsuDevice *d;
    AmtsuDeviceClass *dc;

    switch (addr) {
    case REG_SELECT:
        if (value >= NUM_AMTSU_DEVICES) {
            return MEMTX_ERROR;
        }

        bridge->device = value;
        return MEMTX_OK;
    case REG_MID:
        break;
    case REG_CMD:
        if (bridge->device == 0) {
            return amtsu_bridge_command(bridge, value);
        }

        d = bridge->bus->devices[bridge->device];

        if (d == NULL) {
            return MEMTX_ERROR;
        }

        dc = AMTSU_DEVICE_GET_CLASS(d);

        if (dc->cmd_write) {
            return dc->cmd_write(d, value);
        }

        return MEMTX_OK;
    case REG_A:
        if (bridge->device == 0) {
            bridge->dataA = value;
            return MEMTX_OK;
        }

        d = bridge->bus->devices[bridge->device];

        if (d == NULL) {
            return MEMTX_ERROR;
        }

        d->dataA = value;
        return MEMTX_OK;
    case REG_B:
        if (bridge->device == 0) {
            bridge->dataB = value;
            return MEMTX_OK;
        }

        d = bridge->bus->devices[bridge->device];

        if (d == NULL) {
            return MEMTX_ERROR;
        }

        d->dataB = value;
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static const MemoryRegionOps amtsu_bridge_ops = {
    .read_with_attrs = amtsu_bridge_read,
    .write_with_attrs = amtsu_bridge_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.max_access_size = 4,
    .impl.max_access_size = 4,
    .impl.min_access_size = 4,
};

static void amtsu_bridge_reset(DeviceState *dev)
{
    AmtsuBridge *d = AMTSU_BRIDGE(dev);

    d->device = 0;
}

static void amtsu_bridge_realize(DeviceState *dev, Error **errp)
{
    AmtsuBridge *d = AMTSU_BRIDGE(dev);
    int i;

    memory_region_init_io(&d->mmio, OBJECT(dev), &amtsu_bridge_ops, d, "amtsu",
        0x14);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &d->mmio);

    d->bus = AMTSU_BUS(qbus_new(TYPE_AMTSU_BUS, dev, NULL));

    for (i = 1; i < NUM_AMTSU_DEVICES; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(dev), &d->bus->irq[i - 1]);
    }
}

static const VMStateDescription vmstate_amtsu_bridge = {
    .name = "amtsu_bridge",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(device, AmtsuBridge),
        VMSTATE_UINT32(dataA, AmtsuBridge),
        VMSTATE_UINT32(dataB, AmtsuBridge),
        VMSTATE_END_OF_LIST(),
    },
};

static void amtsu_bridge_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, amtsu_bridge_reset);
    dc->realize = amtsu_bridge_realize;
    dc->vmsd = &vmstate_amtsu_bridge;
    set_bit(DEVICE_CATEGORY_BRIDGE, dc->categories);
}

static const TypeInfo amtsu_bridge_info = {
    .name = TYPE_AMTSU_BRIDGE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = amtsu_bridge_class_init,
    .instance_size = sizeof(AmtsuBridge),
};

static void amtsu_register_types(void)
{
    type_register_static(&amtsu_bus_info);
    type_register_static(&amtsu_device_info);
    type_register_static(&amtsu_bridge_info);
}

type_init(amtsu_register_types)

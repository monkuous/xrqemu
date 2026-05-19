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
#include "ui/console.h"
#include "ui/input.h"
#include "system/runstate.h"

static void amtsu_device_reset(DeviceState *dev)
{
    AmtsuDevice *d = AMTSU_DEVICE(dev);
    AmtsuDeviceClass *dc = AMTSU_DEVICE_GET_CLASS(d);

    d->irqs_enabled = false;
    d->dataA = 0;
    d->dataB = 0;

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
    d->dataA = 0;
    d->dataB = 0;
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

static uint8_t qcode_to_amtsu_kbd[] = {
    [Q_KEY_CODE_A] = 0x01,
	[Q_KEY_CODE_B] = 0x02,
	[Q_KEY_CODE_C] = 0x03,
	[Q_KEY_CODE_D] = 0x04,
	[Q_KEY_CODE_E] = 0x05,
	[Q_KEY_CODE_F] = 0x06,
	[Q_KEY_CODE_G] = 0x07,
	[Q_KEY_CODE_H] = 0x08,
	[Q_KEY_CODE_I] = 0x09,
	[Q_KEY_CODE_J] = 0x0a,
	[Q_KEY_CODE_K] = 0x0b,
	[Q_KEY_CODE_L] = 0x0c,
	[Q_KEY_CODE_M] = 0x0d,
	[Q_KEY_CODE_N] = 0x0e,
	[Q_KEY_CODE_O] = 0x0f,
	[Q_KEY_CODE_P] = 0x10,
	[Q_KEY_CODE_Q] = 0x11,
	[Q_KEY_CODE_R] = 0x12,
	[Q_KEY_CODE_S] = 0x13,
	[Q_KEY_CODE_T] = 0x14,
	[Q_KEY_CODE_U] = 0x15,
	[Q_KEY_CODE_V] = 0x16,
	[Q_KEY_CODE_W] = 0x17,
	[Q_KEY_CODE_X] = 0x18,
	[Q_KEY_CODE_Y] = 0x19,
	[Q_KEY_CODE_Z] = 0x1a,

	[Q_KEY_CODE_0] = 0x1b,
	[Q_KEY_CODE_1] = 0x1c,
	[Q_KEY_CODE_2] = 0x1d,
	[Q_KEY_CODE_3] = 0x1e,
	[Q_KEY_CODE_4] = 0x1f,
	[Q_KEY_CODE_5] = 0x20,
	[Q_KEY_CODE_6] = 0x21,
	[Q_KEY_CODE_7] = 0x22,
	[Q_KEY_CODE_8] = 0x23,
	[Q_KEY_CODE_9] = 0x24,

	[Q_KEY_CODE_SEMICOLON] = 0x25,
	[Q_KEY_CODE_SPC]       = 0x26,
	[Q_KEY_CODE_TAB]       = 0x27,

	[Q_KEY_CODE_MINUS]         = 0x28,
	[Q_KEY_CODE_EQUAL]         = 0x29,
	[Q_KEY_CODE_BRACKET_LEFT]  = 0x2a,
	[Q_KEY_CODE_BRACKET_RIGHT] = 0x2b,
	[Q_KEY_CODE_BACKSLASH]     = 0x2c,

	[Q_KEY_CODE_SLASH]        = 0x2e,
	[Q_KEY_CODE_DOT]          = 0x2f,
	[Q_KEY_CODE_APOSTROPHE]   = 0x30,
	[Q_KEY_CODE_COMMA]        = 0x31,
	[Q_KEY_CODE_GRAVE_ACCENT] = 0x32,

	[Q_KEY_CODE_RET]       = 0x33,
	[Q_KEY_CODE_BACKSPACE] = 0x34,
	[Q_KEY_CODE_CAPS_LOCK] = 0x35,
	[Q_KEY_CODE_ESC]       = 0x36,

	[Q_KEY_CODE_LEFT]     = 0x37,
	[Q_KEY_CODE_RIGHT]    = 0x38,
	[Q_KEY_CODE_DOWN]     = 0x39,
	[Q_KEY_CODE_UP]       = 0x3a,

	[Q_KEY_CODE_CTRL]    = 0x51,
	[Q_KEY_CODE_CTRL_R]  = 0x52,
	[Q_KEY_CODE_SHIFT]   = 0x53,
	[Q_KEY_CODE_SHIFT_R] = 0x54,
	[Q_KEY_CODE_ALT]     = 0x55,
	[Q_KEY_CODE_ALT_R]   = 0x56,

	[Q_KEY_CODE_KP_DIVIDE]   = 0x2e,
	[Q_KEY_CODE_KP_SUBTRACT] = 0x28,
	[Q_KEY_CODE_KP_ENTER]    = 0x33,
	[Q_KEY_CODE_KP_0]        = 0x1b,
	[Q_KEY_CODE_KP_1]        = 0x1c,
	[Q_KEY_CODE_KP_2]        = 0x1d,
	[Q_KEY_CODE_KP_3]        = 0x1e,
	[Q_KEY_CODE_KP_4]        = 0x1f,
	[Q_KEY_CODE_KP_5]        = 0x20,
	[Q_KEY_CODE_KP_6]        = 0x21,
	[Q_KEY_CODE_KP_7]        = 0x22,
	[Q_KEY_CODE_KP_8]        = 0x23,
	[Q_KEY_CODE_KP_9]        = 0x24,
	[Q_KEY_CODE_KP_DECIMAL]  = 0x2f,
};

static void amtsu_keyboard_event(DeviceState *dev, QemuConsole *src,
    InputEvent *evt)
{
    AmtsuKeyboard *s = AMTSU_KBD(dev);
    InputKeyEvent *key = evt->u.key.data;
    int qcode;
    int keycode;

    qemu_system_wakeup_request(QEMU_WAKEUP_REASON_OTHER, NULL);
    assert(evt->type == INPUT_EVENT_KIND_KEY);
    qcode = qemu_input_key_value_to_qcode(key->key);

    if (qcode >= ARRAY_SIZE(qcode_to_amtsu_kbd)) {
        return;
    }

    keycode = qcode_to_amtsu_kbd[qcode] - 1;

    if (keycode >= 0) {
        if (key->down) {
            s->press_queued[keycode / 32] |= 1U << (keycode % 32);
            s->pressed[keycode / 32] |= 1U << (keycode % 32);
        } else {
            s->release_queued[keycode / 32] |= 1U << (keycode % 32);
            s->pressed[keycode / 32] &= ~(1U << (keycode % 32));
        }

        amtsu_raise_irq(AMTSU_DEVICE(s));
    }
}

static void amtsu_kbd_reset(AmtsuDevice *dev) {
    AmtsuKeyboard *d = AMTSU_KBD(dev);

    memset(&d->release_queued, 0, sizeof(d->release_queued));
    memset(&d->press_queued, 0, sizeof(d->press_queued));
    memset(&d->pressed, 0, sizeof(d->pressed));
}

static const QemuInputHandler amtsu_keyboard_handler = {
    .name  = "QEMU Amtsu Keyboard",
    .mask  = INPUT_EVENT_MASK_KEY,
    .event = amtsu_keyboard_event,
};

static void amtsu_kbd_realize(AmtsuDevice *dev, Error **errp)
{
    qemu_input_handler_register(DEVICE(dev), &amtsu_keyboard_handler);
}

#define CMD_POP_SCANCODE 1
#define CMD_CHECK_KEY 3

static MemTxResult amtsu_kbd_cmd_write(struct AmtsuDevice *dev, uint32_t cmd)
{
    AmtsuKeyboard *k = AMTSU_KBD(dev);
    uint32_t i, keycode;

    switch (cmd) {
    case CMD_POP_SCANCODE:
        for (i = 0; i < AMTSU_KBD_BITMAP_SIZE; i++) {
            if (k->release_queued[i]) {
                keycode = ctz32(k->release_queued[i]);

                dev->dataA = 0x8000 | (i * 32 + keycode);
                k->release_queued[i] &= ~(1U << keycode);
                k->press_queued[i] &= ~(1U << keycode);
                return MEMTX_OK;
            } else if (k->press_queued[i]) {
                keycode = ctz32(k->press_queued[i]);

                dev->dataA = i * 32 + keycode;
                k->press_queued[i] &= ~(1U << keycode);
                return MEMTX_OK;
            }
        }

        dev->dataA = 0xFFFF;
        break;
    case CMD_RESET:
        amtsu_kbd_reset(dev);
        break;
    case CMD_CHECK_KEY:
        i = dev->dataA;

        if (i <= AMTSU_MAX_KEYCODE) {
            dev->dataA = !!(k->pressed[i / 32] & (1U << (i % 32)));
        } else {
            dev->dataA = 0;
        }

        break;
    }

    return MEMTX_OK;
}

static const VMStateDescription vmstate_amtsu_kbd = {
    .name = "amtsu_kbd",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(parent_obj, AmtsuKeyboard, 0, vmstate_amtsu_device,
                       AmtsuDevice),
        VMSTATE_UINT32_ARRAY(release_queued, AmtsuKeyboard, AMTSU_KBD_BITMAP_SIZE),
        VMSTATE_UINT32_ARRAY(press_queued, AmtsuKeyboard, AMTSU_KBD_BITMAP_SIZE),
        VMSTATE_UINT32_ARRAY(pressed, AmtsuKeyboard, AMTSU_KBD_BITMAP_SIZE),
        VMSTATE_END_OF_LIST()
    }
};

static void amtsu_kbd_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    AmtsuDeviceClass *adc = AMTSU_DEVICE_CLASS(klass);

    adc->reset = amtsu_kbd_reset;
    adc->realize = amtsu_kbd_realize;
    adc->cmd_write = amtsu_kbd_cmd_write;
    dc->vmsd = &vmstate_amtsu_kbd;

    dc->hotpluggable = true;
}

static void amtsu_kbd_instance_init(Object *obj)
{
    AmtsuDevice *d = AMTSU_DEVICE(obj);

    d->model = 0x8fc48fc4;
}

static const TypeInfo amtsu_kbd_info = {
    .name = TYPE_AMTSU_KBD,
    .parent = TYPE_AMTSU_DEVICE,
    .instance_size = sizeof(AmtsuKeyboard),
    .class_init = amtsu_kbd_class_init,
    .instance_init = amtsu_kbd_instance_init,
};

static void amtsu_register_types(void)
{
    type_register_static(&amtsu_bus_info);
    type_register_static(&amtsu_device_info);
    type_register_static(&amtsu_bridge_info);
    type_register_static(&amtsu_kbd_info);
}

type_init(amtsu_register_types)

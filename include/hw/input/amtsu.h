/*
 * QEMU Amtsu keyboard/mouse emulation
 *
 * Copyright (C) 2026 monkuous
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

#ifndef HW_AMTSU_H
#define HW_AMTSU_H

#include "hw/core/sysbus.h"

#define NUM_AMTSU_DEVICES 16

struct AmtsuDevice {
    /*< private >*/
    DeviceState parent_obj;
    int32_t id;
    bool irqs_enabled;

    /*< public >*/
    uint32_t model;
    uint32_t dataA;
    uint32_t dataB;
};

struct AmtsuDeviceClass {
    /*< private >*/
    DeviceClass parent_class;

    /*< public >*/
    void (*realize)(struct AmtsuDevice *dev, Error **errp);
    void (*unrealize)(struct AmtsuDevice *dev);
    void (*reset)(struct AmtsuDevice *dev);
    MemTxResult (*cmd_write)(struct AmtsuDevice *dev, uint32_t cmd);
    MemTxResult (*cmd_read)(struct AmtsuDevice *dev, uint32_t *val);
};

#define TYPE_AMTSU_DEVICE "amtsu-device"
OBJECT_DECLARE_TYPE(AmtsuDevice, AmtsuDeviceClass, AMTSU_DEVICE)

extern const VMStateDescription vmstate_amtsu_device;

void amtsu_raise_irq(AmtsuDevice *dev);
void amtsu_enable_irq(AmtsuDevice *dev);
void amtsu_disable_irq(AmtsuDevice *dev);

struct AmtsuBus {
    /*< private >*/
    BusState parent_obj;
    AmtsuDevice *devices[NUM_AMTSU_DEVICES];

    /*< public >*/
    qemu_irq irq[NUM_AMTSU_DEVICES - 1];
};

#define TYPE_AMTSU_BUS "Amtsu"
OBJECT_DECLARE_SIMPLE_TYPE(AmtsuBus, AMTSU_BUS)

struct AmtsuBridge {
    /*< private >*/
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    AmtsuBus *bus;
    int32_t device;
    uint32_t dataA;
    uint32_t dataB;

    /*< public >*/
};

#define TYPE_AMTSU_BRIDGE "amtsu-bridge"
OBJECT_DECLARE_SIMPLE_TYPE(AmtsuBridge, AMTSU_BRIDGE)

#define AMTSU_MAX_KEYCODE 0x55
#define AMTSU_KBD_BITMAP_SIZE ((AMTSU_MAX_KEYCODE + 32) / 32)

struct AmtsuKeyboard {
    /*< private >*/
    AmtsuDevice parent_obj;

    uint32_t release_queued[AMTSU_KBD_BITMAP_SIZE];
    uint32_t press_queued[AMTSU_KBD_BITMAP_SIZE];
    uint32_t pressed[AMTSU_KBD_BITMAP_SIZE];

    /*< public >*/
};

#define TYPE_AMTSU_KBD "amtsu-kbd"
OBJECT_DECLARE_SIMPLE_TYPE(AmtsuKeyboard, AMTSU_KBD)

struct AmtsuMouse {
    /*< private >*/
    AmtsuDevice parent_obj;

    uint32_t pressed;
    uint32_t released;
    uint32_t buttons;
    int16_t dx;
    int16_t dy;

    /*< public >*/
};

#define TYPE_AMTSU_MOUSE "amtsu-mouse"
OBJECT_DECLARE_SIMPLE_TYPE(AmtsuMouse, AMTSU_MOUSE)

#endif

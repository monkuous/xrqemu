/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * XR/arch UART
 *
 * (c) 2026 monkuous
 *
 */

#include "qemu/osdep.h"
#include "hw/core/irq.h"
#include "hw/core/qdev-properties-system.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "chardev/char-fe.h"
#include "qemu/log.h"
#include "hw/char/xrarch_uart.h"

#define REG_CMD 0x00
#define REG_DATA 0x04

#define CMD_INT_ENABLE 3
#define CMD_INT_DISABLE 4

enum {
    REG_PUT_CHAR      = 0x00,
    REG_BYTES_READY   = 0x04,
    REG_DATA_PTR      = 0x10,
    REG_DATA_LEN      = 0x14,
    REG_DATA_PTR_HIGH = 0x18,
    REG_VERSION       = 0x20,
};

static uint64_t xrarch_uart_read(void *opaque, hwaddr addr, unsigned size)
{
    XRarchUartState *s = opaque;
    uint64_t value = 0;

    switch (addr) {
    case REG_CMD:
        break;
    case REG_DATA:
        if (!fifo8_is_empty(&s->rx_fifo)) {
            value = fifo8_pop(&s->rx_fifo);
        } else {
            value = 0xffff;
        }

        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register read 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }

    return value;
}

static void xrarch_uart_write(void *opaque, hwaddr addr, uint64_t value,
    unsigned size)
{
    XRarchUartState *s = opaque;
    unsigned char c;

    switch (addr) {
    case REG_CMD:
        switch (value) {
        case CMD_INT_ENABLE:
            if (!s->int_enabled) {
                if (!fifo8_is_empty(&s->rx_fifo)) {
                    qemu_set_irq(s->irq, 1);
                }

                s->int_enabled = true;
            }
            break;
        case CMD_INT_DISABLE:
            if (s->int_enabled) {
                if (!fifo8_is_empty(&s->rx_fifo)) {
                    qemu_set_irq(s->irq, 0);
                }

                s->int_enabled = false;
            }
            break;
        default:
            qemu_log_mask(LOG_UNIMP,
                "%s: unimplemented command 0x%" PRIx64 "\n", __func__, addr);
            break;
        }

        break;
    case REG_DATA:
        c = value;
        qemu_chr_fe_write_all(&s->chr, &c, sizeof(c));
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register write 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps xrarch_uart_ops = {
    .read = xrarch_uart_read,
    .write = xrarch_uart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.max_access_size = 4,
    .impl.max_access_size = 4,
    .impl.min_access_size = 4,
};

static int xrarch_uart_can_receive(void *opaque)
{
    XRarchUartState *s = opaque;
    int available = fifo8_num_free(&s->rx_fifo);

    return available;
}

static void xrarch_uart_receive(void *opaque, const uint8_t *buffer, int size)
{
    XRarchUartState *s = opaque;

    g_assert(size <= fifo8_num_free(&s->rx_fifo));

    fifo8_push_all(&s->rx_fifo, buffer, size);

    if (s->int_enabled && !fifo8_is_empty(&s->rx_fifo)) {
        qemu_set_irq(s->irq, 1);
    }
}

static void xrarch_uart_reset(DeviceState *dev)
{
    XRarchUartState *s = XRARCH_UART(dev);

    fifo8_reset(&s->rx_fifo);
    s->int_enabled = false;
}

static void xrarch_uart_realize(DeviceState *dev, Error **errp)
{
    XRarchUartState *s = XRARCH_UART(dev);

    fifo8_create(&s->rx_fifo, XRARCH_UART_BUFFER_SIZE);
    memory_region_init_io(&s->mmio, OBJECT(s), &xrarch_uart_ops, s,
                          "xrarch.uart", 8);

    if (qemu_chr_fe_backend_connected(&s->chr)) {
        qemu_chr_fe_set_handlers(&s->chr, xrarch_uart_can_receive,
                                 xrarch_uart_receive, NULL, NULL,
                                 s, NULL, true);
    }
}

static void xrarch_uart_unrealize(DeviceState *dev)
{
    XRarchUartState *s = XRARCH_UART(dev);

    fifo8_destroy(&s->rx_fifo);
}

static const VMStateDescription vmstate_xrarch_uart = {
    .name = "xrarch_uart",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_BOOL(int_enabled, XRarchUartState),
        VMSTATE_FIFO8(rx_fifo, XRarchUartState),
        VMSTATE_END_OF_LIST()
    }
};

static const Property xrarch_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", XRarchUartState, chr),
};

static void xrarch_uart_instance_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    XRarchUartState *s = XRARCH_UART(obj);

    sysbus_init_mmio(dev, &s->mmio);
    sysbus_init_irq(dev, &s->irq);
}

static void xrarch_uart_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_props(dc, xrarch_uart_properties);
    device_class_set_legacy_reset(dc, xrarch_uart_reset);
    dc->realize = xrarch_uart_realize;
    dc->unrealize = xrarch_uart_unrealize;
    dc->vmsd = &vmstate_xrarch_uart;
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);
}

static const TypeInfo xrarch_uart_info = {
    .name = TYPE_XRARCH_UART,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = xrarch_uart_class_init,
    .instance_init = xrarch_uart_instance_init,
    .instance_size = sizeof(XRarchUartState),
};

static void xrarch_uart_register_types(void)
{
    type_register_static(&xrarch_uart_info);
}

type_init(xrarch_uart_register_types)

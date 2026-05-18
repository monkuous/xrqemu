/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/arch Real Time Clock emulation
 *
 * Copyright (C) 2026 monkuous
 */

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "hw/core/irq.h"
#include "qemu/timer.h"
#include "system/system.h"
#include "migration/vmstate.h"

#define SYS_RTCCMD 0x00
#define SYS_RTCDATA 0x04

#define CMD_SET_INTERVAL 1
#define CMD_GET_SECONDS 2
#define CMD_GET_MILLIS 3
#define CMD_SET_SECONDS 4
#define CMD_SET_MILLIS 5

#define TYPE_XRARCH_RTC "xrarch.rtc"
OBJECT_DECLARE_SIMPLE_TYPE(XRarchRtcState, XRARCH_RTC)

struct XRarchRtcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t data;
    uint32_t interval;
    int64_t offset_rtc;

    int64_t last_time;
    QEMUTimer *timer;
    qemu_irq irq;
};

static void xrarch_rtc_update(XRarchRtcState *s)
{
    if (s->interval == 0) {
        timer_del(s->timer);
        return;
    }

    timer_mod(s->timer, s->last_time + s->interval);
}

static MemTxResult xrarch_rtc_read(void *opaque, hwaddr addr, uint64_t *value,
    unsigned size, MemTxAttrs attrs)
{
    XRarchRtcState *s = XRARCH_RTC(opaque);

    switch (addr) {
    case SYS_RTCCMD:
        *value = 0;
        return MEMTX_OK;
    case SYS_RTCDATA:
        *value = s->data;
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static int64_t get_rtc_time(XRarchRtcState *s) {
    return qemu_clock_get_ms(rtc_clock) - s->offset_rtc;
}

static MemTxResult xrarch_rtc_write(void *opaque, hwaddr addr, uint64_t val,
    unsigned size, MemTxAttrs attrs)
{
    int64_t time;
    XRarchRtcState *s = XRARCH_RTC(opaque);

    switch (addr) {
    case SYS_RTCCMD:
        switch (val) {
        case CMD_SET_INTERVAL:
            s->interval = s->data;
            s->last_time = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
            xrarch_rtc_update(s);
            return MEMTX_OK;
        case CMD_GET_SECONDS:
            s->data = get_rtc_time(s) / 1000;
            return MEMTX_OK;
        case CMD_GET_MILLIS:
            s->data = get_rtc_time(s) % 1000;
            return MEMTX_OK;
        case CMD_SET_SECONDS:
            time = s->data * 1000;
            time += get_rtc_time(s) % 1000;
            s->offset_rtc = time - qemu_clock_get_ms(rtc_clock);
            return MEMTX_OK;
        case CMD_SET_MILLIS:
            time = get_rtc_time(s);
            time -= time % 1000;
            time += s->data % 1000;
            s->offset_rtc = time - qemu_clock_get_ms(rtc_clock);
            return MEMTX_OK;
        }

        break;
    case SYS_RTCDATA:
        s->data = val;
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static const MemoryRegionOps xrarch_rtc_ops = {
    .read_with_attrs = xrarch_rtc_read,
    .write_with_attrs = xrarch_rtc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void rtc_timer_cb(void *opaque)
{
    XRarchRtcState *s = opaque;
    s->last_time += s->interval;
    xrarch_rtc_update(s);
    qemu_irq_raise(s->irq);
}

static void xrarch_rtc_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    XRarchRtcState *d = XRARCH_RTC(sbd);
    memory_region_init_io(&d->iomem, NULL, &xrarch_rtc_ops,
                         (void *)d, "xrarch.rtc", 8);

    sysbus_init_irq(sbd, &d->irq);

    sysbus_init_mmio(sbd, &d->iomem);
    d->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, rtc_timer_cb, d);
}

/* delete timer and clear reg when reset */
static void xrarch_rtc_reset(DeviceState *dev)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    XRarchRtcState *d = XRARCH_RTC(sbd);
    timer_del(d->timer);
    d->interval = 0;
}

static int xrarch_rtc_pre_save(void *opaque)
{
    XRarchRtcState *s = XRARCH_RTC(opaque);

    timer_del(s->timer);

    return 0;
}

static int xrarch_rtc_post_load(void *opaque, int version_id)
{
    XRarchRtcState *s = XRARCH_RTC(opaque);

    xrarch_rtc_update(s);

    return 0;
}

static const VMStateDescription vmstate_xrarch_rtc = {
    .name = "xrarch_rtc",
    .version_id = 1,
    .minimum_version_id = 1,
    .pre_save = xrarch_rtc_pre_save,
    .post_load = xrarch_rtc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(data, XRarchRtcState),
        VMSTATE_UINT32(interval, XRarchRtcState),
        VMSTATE_INT64(offset_rtc, XRarchRtcState),
        VMSTATE_INT64(last_time, XRarchRtcState),
        VMSTATE_END_OF_LIST()
    }
};

static void xrarch_rtc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->vmsd = &vmstate_xrarch_rtc;
    dc->realize = xrarch_rtc_realize;
    device_class_set_legacy_reset(dc, xrarch_rtc_reset);
    dc->desc = "XRarch rtc";
}

static const TypeInfo xrarch_rtc_info = {
    .name          = TYPE_XRARCH_RTC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XRarchRtcState),
    .class_init    = xrarch_rtc_class_init,
};

static void xrarch_rtc_register_types(void)
{
    type_register_static(&xrarch_rtc_info);
}

type_init(xrarch_rtc_register_types)

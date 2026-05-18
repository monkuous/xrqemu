/*
 * XR/arch LSIC (Local Symmetric Interrupt Controller)
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

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/error-report.h"
#include "hw/core/sysbus.h"
#include "hw/pci/msi.h"
#include "hw/core/qdev-properties.h"
#include "hw/intc/xrarch_lsic.h"
#include "target/xr17032/cpu.h"
#include "migration/vmstate.h"
#include "hw/core/irq.h"

static int xrarch_lsic_claimed(XRarchLSICState *lsic, uint32_t targetid)
{
    uint32_t priority = lsic->priorities[targetid];
    uint32_t max_idx = (priority + 31) >> 5;
    int i, j;

    for (i = 0; i < max_idx; i++) {
        uint32_t index = targetid * 2 + i;
        uint32_t pending_enabled = lsic->pending[index] & lsic->disable[index];

        if (!pending_enabled) {
            continue;
        }

        for (j = 0; j < 32; j++) {
            int irq = (i << 5) + j;
            int enabled = pending_enabled & (1 << j);

            if (irq != 0 && enabled && irq < priority) {
                return irq;
            }
        }
    }

    return 0;
}

static void xrarch_lsic_update(XRarchLSICState *lsic, uint32_t targetid)
{
    bool level = !!xrarch_lsic_claimed(lsic, targetid);
    qemu_set_irq(lsic->targets[targetid], level);
}

static uint64_t xrarch_lsic_read(void *opaque, hwaddr addr, unsigned size)
{
    XRarchLSICState *lsic = opaque;
    uint32_t targetid = addr / XRARCH_LSIC_STRIDE;

    if (targetid < lsic->num_targets) {
        switch (targetid % XRARCH_LSIC_STRIDE) {
        case 0: /* DISA0 */
            return lsic->disable[targetid * 2];
        case 4: /* DISA1 */
            return lsic->disable[targetid * 2 + 1];
        case 8: /* PEND0 */
            return lsic->pending[targetid * 2];
        case 12: /* PEND1 */
            return lsic->pending[targetid * 2 + 1];
        case 16: /* CLAIM */
            return xrarch_lsic_claimed(lsic, targetid);
        case 20: /* IPL */
            return lsic->priorities[targetid];
        default:
            break;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                "%s: Invalid register read 0x%" HWADDR_PRIx "\n",
                __func__, addr);
    return 0;
}

static void xrarch_lsic_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    XRarchLSICState *lsic = opaque;

    uint32_t targetid = addr / XRARCH_LSIC_STRIDE;

    if (targetid < lsic->num_targets) {
        switch (targetid % XRARCH_LSIC_STRIDE) {
        case 0: /* DISA0 */
            lsic->disable[targetid * 2] = value;

            xrarch_lsic_update(lsic, targetid);
            return;
        case 4: /* DISA1 */
            lsic->disable[targetid * 2 + 1] = value;

            xrarch_lsic_update(lsic, targetid);
            return;
        case 8: /* PEND0 */
            if (value == 0) {
                lsic->pending[targetid * 2] = 0;
            } else {
                qatomic_or(&lsic->pending[targetid * 2], value & ~1U);
            }

            xrarch_lsic_update(lsic, targetid);
            return;
        case 12: /* PEND1 */
            if (value == 0) {
                lsic->pending[targetid * 2 + 1] = 0;
            } else {
                qatomic_or(&lsic->pending[targetid * 2 + 1], value);
            }

            xrarch_lsic_update(lsic, targetid);
            return;
        case 16: /* CLAIM */
            if (value >= 64) {
                break;
            }

            qatomic_and(&lsic->pending[targetid * 2 + (value >> 5)], ~(1U << (value & 31)));
            xrarch_lsic_update(lsic, targetid);
            return;
        case 20: /* IPL */
            if (value >= 64) {
                break;
            }

            lsic->priorities[targetid] = value;
            xrarch_lsic_update(lsic, targetid);
            return;
        default:
            break;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: Invalid register write 0x%" HWADDR_PRIx
                  " (0x%" PRIx64 ")\n",
                  __func__, addr, value);
}

static const MemoryRegionOps xrarch_lsic_ops = {
    .read = xrarch_lsic_read,
    .write = xrarch_lsic_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void xrarch_lsic_reset(DeviceState *dev)
{
    XRarchLSICState *s = XRARCH_LSIC(dev);
    int i;

    memset(s->priorities, 0, sizeof(uint32_t) * s->num_targets);
    memset(s->pending, 0, sizeof(uint32_t) * s->num_masks);
    memset(s->disable, 0, sizeof(uint32_t) * s->num_masks);

    for (i = 0; i < s->num_targets; i++) {
        qemu_set_irq(s->targets[i], 0);
    }
}

static void xrarch_lsic_irq_request(void *opaque, int irq, int level)
{
    XRarchLSICState *s = opaque;

    if (level > 0) {
        for (uint32_t targetid = 0; targetid < s->num_targets; targetid++) {
            qatomic_or(&s->pending[targetid * 2 + (irq >> 5)], 1U << (irq & 31));
            xrarch_lsic_update(s, targetid);
        }
    }
}

static void xrarch_lsic_realize(DeviceState *dev, Error **errp)
{
    XRarchLSICState *s = XRARCH_LSIC(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &xrarch_lsic_ops, s,
                          TYPE_XRARCH_LSIC, s->num_targets * XRARCH_LSIC_STRIDE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);

    s->num_masks = s->num_targets * 2;
    s->priorities = g_new0(uint32_t, s->num_targets);
    s->pending = g_new0(uint32_t, s->num_masks);
    s->disable = g_new0(uint32_t, s->num_masks);

    qdev_init_gpio_in(dev, xrarch_lsic_irq_request, 64);

    s->targets = g_malloc(sizeof(qemu_irq) * s->num_targets);
    qdev_init_gpio_out(dev, s->targets, s->num_targets);

    msi_nonbroken = true;
}

static const VMStateDescription vmstate_xrarch_lsic = {
    .name = "xrarch_lsic",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
            VMSTATE_VARRAY_UINT32(priorities, XRarchLSICState,
                                  num_targets, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(pending, XRarchLSICState, num_masks, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_VARRAY_UINT32(disable, XRarchLSICState, num_masks, 0,
                                  vmstate_info_uint32, uint32_t),
            VMSTATE_END_OF_LIST()
        }
};

static const Property xrarch_lsic_properties[] = {
    DEFINE_PROP_UINT32("num-targets", XRarchLSICState, num_targets, 0),
};

static void xrarch_lsic_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, xrarch_lsic_reset);
    device_class_set_props(dc, xrarch_lsic_properties);
    dc->realize = xrarch_lsic_realize;
    dc->vmsd = &vmstate_xrarch_lsic;
}

static const TypeInfo xrarch_lsic_info = {
    .name          = TYPE_XRARCH_LSIC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(XRarchLSICState),
    .class_init    = xrarch_lsic_class_init,
};

static void xrarch_lsic_register_types(void)
{
    type_register_static(&xrarch_lsic_info);
}

type_init(xrarch_lsic_register_types)

/*
 * Create LSIC device.
 */
DeviceState *xrarch_lsic_create(hwaddr addr, uint32_t num_targets)
{
    DeviceState *dev = qdev_new(TYPE_XRARCH_LSIC);
    int i;
    XRarchLSICState *lsic;

    qdev_prop_set_uint32(dev, "num-targets", num_targets);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, addr);

    lsic = XRARCH_LSIC(dev);

    for (i = 0; i < lsic->num_targets; i++) {
        CPUState *cpu = qemu_get_cpu(i);

        qdev_connect_gpio_out(dev, i,
                              qdev_get_gpio_in(DEVICE(cpu), 0));
    }

    return dev;
}

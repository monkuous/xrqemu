/*
 * QEMU XR/arch kinnowfb emulator.
 *
 * Copyright (c) 2026 monkuous
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/core/hw-error.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "trace.h"
#include "hw/core/sysbus.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "hw/misc/ebus.h"

#define KINNOWFB_WIDTH 1024
#define KINNOWFB_HEIGHT 768

#define KINNOWFB_VRAM (KINNOWFB_WIDTH * KINNOWFB_HEIGHT)
#define KINNOWFB_FEATURES 0b1 /* writable palette */

union SlotInfo {
    struct {
        uint32_t magic;
        uint32_t board_id;
        char name[16];
    };
    uint8_t data8[256];
    uint16_t data16[128];
    uint32_t data32[64];
    uint64_t data64[32];
};

static const union SlotInfo kinnowfb_slot_info = {
    .magic = 0x0c007ca1,
    .board_id = 0x4b494e36,
    .name = "kinnowfb,8"
};

struct KinnowfbState {
    SysBusDevice parent_obj;

    /* hardware */
    MemoryRegion mem;
    MemoryRegion mem_ctrl;
    MemoryRegion mem_vram;
    /* registers */
    uint32_t palette[256];
    uint32_t control[64];
    /* display refresh support */
    QemuConsole *con;
};

#define TYPE_KINNOWFB "kinnowfb"
OBJECT_DECLARE_SIMPLE_TYPE(KinnowfbState, KINNOWFB)

#define REG_SIZE 0
#define REG_VRAM 1
#define REG_FEAT 8

#define KINNOWFB_PAGE_SIZE 4096

static inline int check_dirty(KinnowfbState *s, DirtyBitmapSnapshot *snap,
    ram_addr_t page)
{
    return memory_region_snapshot_get_dirty(&s->mem_vram, snap, page,
        KINNOWFB_PAGE_SIZE);
}

static void kinnowfb_draw_graphics(KinnowfbState *s)
{
    DisplaySurface *surface = qemu_console_surface(s->con);
    DirtyBitmapSnapshot *snap;
    int i, w;
    uint8_t *vram;
    uint8_t *data_display, *dd;
    ram_addr_t page;
    int x, y;
    int xmin, xmax;
    int ymin, ymax;
    unsigned int (*rgb_to_pixel)(unsigned int r, unsigned int g, unsigned int b);

    switch (surface_bits_per_pixel(surface)) {
        case 8:
            rgb_to_pixel = rgb_to_pixel8;
            w = 1;
            break;
        case 15:
            rgb_to_pixel = rgb_to_pixel15;
            w = 2;
            break;
        case 16:
            rgb_to_pixel = rgb_to_pixel16;
            w = 2;
            break;
        case 32:
            rgb_to_pixel = rgb_to_pixel32;
            w = 4;
            break;
        default:
            hw_error("kinnowfb: unknown host depth %d",
                     surface_bits_per_pixel(surface));
            return;
    }

    page = 0;

    x = y = 0;
    xmin = KINNOWFB_WIDTH;
    xmax = 0;
    ymin = KINNOWFB_HEIGHT;
    ymax = 0;

    vram = memory_region_get_ram_ptr(&s->mem_vram);
    data_display = dd = surface_data(surface);
    snap = memory_region_snapshot_and_clear_dirty(&s->mem_vram, 0,
        KINNOWFB_VRAM, DIRTY_MEMORY_VGA);

    while (y < KINNOWFB_HEIGHT) {
        if (check_dirty(s, snap, page)) {
            if (y < ymin)
                ymin = ymax = y;

            if (x < xmin)
                xmin = x;

            for (i = 0; i < KINNOWFB_PAGE_SIZE; i++) {
                uint8_t index = *vram;
                unsigned int color = s->palette[index];
                color = (*rgb_to_pixel)((color >> 16) & 0xff,
                    (color >> 8) & 0xff, color & 0xff);
                memcpy(dd, &color, w);
                dd += w;
                x++;
                vram++;

                if (x == KINNOWFB_WIDTH) {
                    xmax = KINNOWFB_WIDTH - 1;
                    y++;

                    if (y == KINNOWFB_HEIGHT) {
                        ymax = KINNOWFB_HEIGHT - 1;
                        goto done;
                    }

                    data_display = dd = data_display + surface_stride(surface);
                    xmin = 0;
                    x = 0;
                }
            }

            if (x > xmax)
                xmax = x;

            if (y > ymax)
                ymax = y;
        } else {
            int dy;

            if (xmax || ymax) {
                dpy_gfx_update(s->con, xmin, ymin,
                               xmax - xmin + 1, ymax - ymin + 1);
                xmin = KINNOWFB_WIDTH;
                xmax = 0;
                ymin = KINNOWFB_HEIGHT;
                ymax = 0;
            }

            x += KINNOWFB_PAGE_SIZE;
            dy = x / KINNOWFB_WIDTH;
            x = x % KINNOWFB_WIDTH;
            vram += KINNOWFB_PAGE_SIZE;

            y += dy;

            data_display += dy * surface_stride(surface);
            dd = data_display + x * w;
        }

        page += KINNOWFB_PAGE_SIZE;
    }

done:
    if (xmax || ymax) {
        dpy_gfx_update(s->con, xmin, ymin, xmax - xmin + 1, ymax - ymin + 1);
    }

    g_free(snap);
}

static void kinnowfb_update_display(void *opaque)
{
    KinnowfbState *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);

    qemu_flush_coalesced_mmio_buffer();

    if (KINNOWFB_WIDTH != surface_width(surface) ||
        KINNOWFB_HEIGHT != surface_height(surface)) {
        qemu_console_resize(s->con, KINNOWFB_WIDTH, KINNOWFB_HEIGHT);
    }

    kinnowfb_draw_graphics(s);
}

static inline void kinnowfb_invalidate_display(void *opaque)
{
    KinnowfbState *s = opaque;

    memory_region_set_dirty(&s->mem_vram, 0, KINNOWFB_VRAM);
}

static MemTxResult kinnowfb_ctrl_read(void *opaque, hwaddr addr, 
    uint64_t *value, unsigned int size, MemTxAttrs attrs)
{
    KinnowfbState *s = opaque;

    if ((addr & (size - 1)) != 0) {
        return MEMTX_ERROR;
    }

    if (addr < 0x100) {
        switch (size) {
        case 1:
            *value = kinnowfb_slot_info.data8[addr];
            return MEMTX_OK;
        case 2:
            *value = kinnowfb_slot_info.data16[addr / 2];
            return MEMTX_OK;
        case 4:
            *value = kinnowfb_slot_info.data32[addr / 4];
            return MEMTX_OK;
        case 8:
            *value = kinnowfb_slot_info.data64[addr / 8];
            return MEMTX_OK;
        }

        return MEMTX_ERROR;
    }

    if (size != 4) {
        return MEMTX_ERROR;
    }

    if (addr >= 0x3000 && addr < 0x3100) {
        *value = s->control[(addr - 0x3000) / 4];
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static MemTxResult kinnowfb_ctrl_write(void *opaque, hwaddr addr, uint64_t value,
    unsigned int size, MemTxAttrs attrs)
{
    KinnowfbState *s = opaque;

    if ((addr & (size - 1)) != 0 || size != 4) {
        return MEMTX_ERROR;
    }

    if (addr >= 0x3000 && addr < 0x3100) {
        s->control[(addr - 0x3000) / 4] = value;
        return MEMTX_OK;
    }

    if (addr >= 0x4000 && addr < 0x4400) {
        s->palette[(addr - 0x4000) / 4] = value;
        kinnowfb_invalidate_display(s);
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static const MemoryRegionOps kinnowfb_ctrl_ops = {
    .read_with_attrs = kinnowfb_ctrl_read,
    .write_with_attrs = kinnowfb_ctrl_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl.min_access_size = 1,
    .impl.max_access_size = 8,
};

static const GraphicHwOps kinnowfb_ops = {
    .invalidate  = kinnowfb_invalidate_display,
    .gfx_update  = kinnowfb_update_display,
};

static int kinnowfb_id;

static void kinnowfb_realize(DeviceState *dev, Error **errp)
{
    KinnowfbState *s = KINNOWFB(dev);
    int id = qatomic_fetch_add(&kinnowfb_id, 1);
    char vram_name[40];

    s->con = graphic_console_init(dev, 0, &kinnowfb_ops, s);

    snprintf(vram_name, sizeof(vram_name), "kinnowfb.vram%d", id);

    memory_region_init_io(&s->mem_ctrl, NULL, &kinnowfb_ctrl_ops, s,
                          "kinnowfb.ctrl", 0x180000);
    memory_region_init_ram(&s->mem_vram, NULL, vram_name, KINNOWFB_VRAM,
                           &error_fatal);
    memory_region_set_log(&s->mem_vram, true, DIRTY_MEMORY_VGA);

    memory_region_init(&s->mem, OBJECT(dev), "kinnowfb", EBUS_APERTURE);
    memory_region_add_subregion(&s->mem, 0, &s->mem_ctrl);
    memory_region_add_subregion(&s->mem, 0x100000, &s->mem_vram);
}

static void kinnowfb_reset(DeviceState *d)
{
    KinnowfbState *s = KINNOWFB(d);

    uint8_t *vram = memory_region_get_ram_ptr(&s->mem_vram);

    memset(s->palette, 0, sizeof(s->palette));
    memset(s->control, 0, sizeof(s->control));
    memset(vram, 0, KINNOWFB_VRAM);
    kinnowfb_invalidate_display(s);

    s->control[REG_SIZE] = (KINNOWFB_HEIGHT << 12) | KINNOWFB_WIDTH;
    s->control[REG_VRAM] = KINNOWFB_VRAM;
    s->control[REG_FEAT] = KINNOWFB_FEATURES;
}

static int kinnowfb_post_load(void *opaque, int version_id)
{
    KinnowfbState *s = opaque;

    /* force refresh */
    kinnowfb_invalidate_display(s);

    return 0;
}

static const VMStateDescription vmstate_kinnowfb = {
    .name = "kinnowfb",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = kinnowfb_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(palette, KinnowfbState, 256),
        VMSTATE_UINT32_ARRAY(control, KinnowfbState, 64),
        VMSTATE_END_OF_LIST()
    }
};

static void kinnowfb_instance_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    KinnowfbState *s = KINNOWFB(obj);

    sysbus_init_mmio(dev, &s->mem);
}

static void kinnowfb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, kinnowfb_reset);
    dc->realize = kinnowfb_realize;
    dc->desc = "Kinnow framebuffer";
    dc->vmsd = &vmstate_kinnowfb;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo kinnowfb_info = {
    .name = TYPE_KINNOWFB,
    .parent = TYPE_EBUS_DEVICE,
    .class_init = kinnowfb_class_init,
    .instance_init = kinnowfb_instance_init,
    .instance_size = sizeof(KinnowfbState),
};

static void kinnowfb_register_types(void)
{
    type_register_static(&kinnowfb_info);
}

type_init(kinnowfb_register_types)

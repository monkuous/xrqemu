/*
 * QEMU XR/computer Board
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
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "target/xr17032/cpu.h"
#include "hw/xr17032/xrcomputer.h"
#include "hw/intc/xrarch_lsic.h"
#include "hw/rtc/xrarch_rtc.h"
#include "hw/char/xrarch_uart.h"
#include "hw/block/xrarch_disk.h"
#include "hw/input/amtsu.h"
#include "system/system.h"
#include "system/reset.h"
#include "qemu/datadir.h"
#include "hw/core/qdev-properties.h"
#include "hw/misc/ebus.h"
#include "qemu/option.h"

#define XRCOMPUTER_CPUS_MAX 4

#define RAM_SLOT_SIZE (32 * 1024 * 1024)
#define NUM_RAM_SLOTS 8
#define MAX_RAM (RAM_SLOT_SIZE * NUM_RAM_SLOTS)

#define BOARD_COUNT 7

#define RESET_MAGIC 0xaabbccdd

#define XRCOMPUTER_NVRAM_SECTOR_SIZE 4096

enum {
    XRCOMPUTER_DRAM,
    XRCOMPUTER_EBUS,
    XRCOMPUTER_UART0,
    XRCOMPUTER_UART1,
    XRCOMPUTER_DISK,
    XRCOMPUTER_RTC,
    XRCOMPUTER_AMTSU,
    XRCOMPUTER_BOARD,
    XRCOMPUTER_NVRAM,
    XRCOMPUTER_LSIC,
    XRCOMPUTER_RESET,
    XRCOMPUTER_FW,
};

enum {
    RTC_IRQ = 2,
    DISK_IRQ = 3,
    UART0_IRQ = 4,
    UART1_IRQ = 5,
    EBUS_IRQ = 0x28, /* 0x28-0x2e */
    AMTSU_IRQ = 0x30, /* 0x30-0x33 */
};

#define LSIC_SPACE 0x100000

#if XRCOMPUTER_CPUS_MAX * XRARCH_LSIC_STRIDE > LSIC_SPACE
#error "Too many CPUs, cannot place LSICs"
#endif

static const MemMapEntry xrcomputer_memmap[] = {
    [XRCOMPUTER_DRAM] =  {        0x0,        0x0 },
    [XRCOMPUTER_EBUS] =  { 0xc0000000, EBUS_APERTURE },
    [XRCOMPUTER_UART0] = { 0xf8000040,        0x8 },
    [XRCOMPUTER_UART1] = { 0xf8000048,        0x8 },
    [XRCOMPUTER_DISK] =  { 0xf8000064,        0xc },
    [XRCOMPUTER_RTC] =   { 0xf8000080,        0x8 },
    [XRCOMPUTER_AMTSU] = { 0xf80000c0,       0x14 },
    [XRCOMPUTER_BOARD] = { 0xf8000800,       0x80 },
    [XRCOMPUTER_NVRAM] = { 0xf8001000,     0x1000 },
    [XRCOMPUTER_LSIC] =  { 0xf8030000, LSIC_SPACE },
    [XRCOMPUTER_RESET] = { 0xf8800000,        0x4 },
    [XRCOMPUTER_FW] =    { 0xfffe0000,    0x20000 },
};

static void xrcomputer_done(Notifier *notifier, void *data)
{
    XRcomputerState *s = container_of(notifier, XRcomputerState, machine_done);
    MachineState *machine = MACHINE(s);
    const char *firmware_name = machine->firmware;
    char *bios_name;
    ssize_t bios_size;

    if (firmware_name) {
        bios_name = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware_name);

        if (!bios_name) {
            error_report("Could not find ROM image '%s'", firmware_name);
            exit(1);
        }

        bios_size = load_image_mr(bios_name, &s->fw_rom);

        if (bios_size < 0) {
            error_report("Could not load ROM image '%s'", bios_name);
            exit(1);
        }

        g_free(bios_name);
    }
}

static void xr17032_cpus_reset(void *opaque)
{
    DeviceState *cpu = opaque;
    cpu_reset(CPU(cpu));
}

static MemTxResult reset_read(void *opaque, hwaddr addr, uint64_t *data,
    unsigned size, MemTxAttrs attrs)
{
    return MEMTX_ERROR;
}

static MemTxResult reset_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size, MemTxAttrs attrs)
{
    if (value == RESET_MAGIC) {
        bus_cold_reset(sysbus_get_default());
        return MEMTX_OK;
    }

    return MEMTX_ERROR;
}

static const MemoryRegionOps reset_ops = {
    .read_with_attrs = reset_read,
    .write_with_attrs = reset_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static uint64_t board_read(void *opaque, hwaddr addr, unsigned size)
{
    XRcomputerState *s = opaque;

    return s->revision_data[addr / 4];
}

static void board_write(void *opaque, hwaddr addr, uint64_t value,
        unsigned size)
{
    XRcomputerState *s = opaque;

    if (addr != 0) {
        s->revision_data[addr / 4] = value;
    }
}

static const MemoryRegionOps board_ops = {
    .read = board_read,
    .write = board_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void create_serial(DeviceState *irqchip, int memmap, int irq, int id)
{
    DeviceState *dev = qdev_new(TYPE_XRARCH_UART);

    qdev_prop_set_chr(dev, "chardev", serial_hd(id));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(irqchip, irq));
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, xrcomputer_memmap[memmap].base);
}

static void xrcomputer_init(MachineState *machine)
{
    XRcomputerState *s = XRCOMPUTER_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *dev;
    int i;
    qemu_irq irq;
    BusState *bus;
    DriveInfo *dinfo;
    MemoryRegion *mr;

    s->revision_data[0] = 0x00030001; /* pboard version */

    if (machine->ram_size > MAX_RAM) {
        machine->ram_size = MAX_RAM;
        error_report("Limiting RAM size to %" HWADDR_PRIu " bytes", machine->ram_size);
    }

    /* initialize cpus */
    for (i = 0; i < machine->smp.cpus; i++) {
        dev = qdev_new(machine->cpu_type);

        XR17032_CPU(dev)->phy_id = i;
        qemu_register_reset(xr17032_cpus_reset, dev);

        if (!qdev_realize_and_unref(dev, NULL, &error_fatal)) {
            return;
        }
    }

    /* initialize platform mmio */
    memory_region_init_io(&s->reset_mmio, NULL, &reset_ops, s,
        "xrcomputer.reset", xrcomputer_memmap[XRCOMPUTER_RESET].size);
    memory_region_add_subregion(system_memory,
        xrcomputer_memmap[XRCOMPUTER_RESET].base, &s->reset_mmio);
    memory_region_init_io(&s->board_mmio, NULL, &board_ops, s,
        "xrcomputer.board", xrcomputer_memmap[XRCOMPUTER_BOARD].size);
    memory_region_add_subregion(system_memory,
        xrcomputer_memmap[XRCOMPUTER_BOARD].base, &s->board_mmio);

    /* initialize irqchip */
    s->irqchip = xrarch_lsic_create(xrcomputer_memmap[XRCOMPUTER_LSIC].base,
        machine->smp.cpus);

    /* initialize rtc */
    sysbus_create_simple(TYPE_XRARCH_RTC, xrcomputer_memmap[XRCOMPUTER_RTC].base,
        qdev_get_gpio_in(s->irqchip, RTC_IRQ));

    /* initialize serial ports */
    create_serial(s->irqchip, XRCOMPUTER_UART0, UART0_IRQ, 0);
    create_serial(s->irqchip, XRCOMPUTER_UART1, UART1_IRQ, 1);

    /* initialize nvram */
    /* TODO: persistence */
    mr = g_new(MemoryRegion, 1);
    memory_region_init_ram(mr, NULL, "xrcomputer.nvram",
        xrcomputer_memmap[XRCOMPUTER_NVRAM].size, &error_fatal);
    memory_region_add_subregion(system_memory,
        xrcomputer_memmap[XRCOMPUTER_NVRAM].base, mr);

    /* initialize amtsu */
    dev = qdev_new(TYPE_AMTSU_BRIDGE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0,
        xrcomputer_memmap[XRCOMPUTER_AMTSU].base);

    for (i = 0; i < 4; i++) {
        irq = qdev_get_gpio_in(s->irqchip, AMTSU_IRQ + i);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
    }

    s->amtsu = BUS(AMTSU_BRIDGE(dev)->bus);

    /* initialize disk */
    dev = sysbus_create_simple(TYPE_XRARCH_DISK_CTRL,
        xrcomputer_memmap[XRCOMPUTER_DISK].base,
        qdev_get_gpio_in(s->irqchip, DISK_IRQ));
    bus = BUS(&XRARCH_DISK_CTRL(dev)->bus);

    for (i = 0; i < XRARCH_MAX_DISK; i++) {
        dinfo = drive_get(IF_XRDISK, 0, i);

        if (!dinfo) {
            continue;
        }

        dev = qdev_new(TYPE_XRARCH_DISK);
        XRARCH_DISK(dev)->unit = i;

        qdev_prop_set_drive_err(dev, "drive", blk_by_legacy_dinfo(dinfo),
            &error_fatal);

        if (!qdev_realize_and_unref(dev, bus, &error_fatal)) {
            return;
        }
    }

    if (!s->headless) {
        /* add kinnowfb */
        dev = qdev_new("kinnowfb");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

        /* add keyboard */
        dev = qdev_new(TYPE_AMTSU_KBD);
        qdev_realize_and_unref(dev, s->amtsu, &error_fatal);
    }

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory,
        xrcomputer_memmap[XRCOMPUTER_DRAM].base, machine->ram);

    /* add firmware rom region */
    memory_region_init_rom(&s->fw_rom, NULL, "xrcomputer.firmware",
        xrcomputer_memmap[XRCOMPUTER_FW].size, &error_fatal);
    memory_region_add_subregion(system_memory,
        xrcomputer_memmap[XRCOMPUTER_FW].base, &s->fw_rom);

    s->machine_done.notify = xrcomputer_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static const CPUArchIdList *xrcomputer_possible_cpu_arch_ids(MachineState *ms)
{
    int n;
    unsigned int max_cpus = ms->smp.max_cpus;

    if (ms->possible_cpus) {
        assert(ms->possible_cpus->len == max_cpus);
        return ms->possible_cpus;
    }

    ms->possible_cpus = g_malloc0(sizeof(CPUArchIdList) +
                                  sizeof(CPUArchId) * max_cpus);
    ms->possible_cpus->len = max_cpus;

    for (n = 0; n < ms->possible_cpus->len; n++) {
        ms->possible_cpus->cpus[n].type = ms->cpu_type;
        ms->possible_cpus->cpus[n].arch_id = n;
    }

    return ms->possible_cpus;
}

static CpuInstanceProperties xrcomputer_cpu_index_to_props(MachineState *ms,
    unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static HotplugHandler *xrcomputer_get_hotplug_handler(MachineState *machine,
    DeviceState *dev)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);

    if (device_is_dynamic_sysbus(mc, dev)) {
        return HOTPLUG_HANDLER(machine);
    }

    return NULL;
}

static void xrcomputer_device_plug_cb(HotplugHandler *hotplug_dev,
    DeviceState *dev, Error **errp)
{
    XRcomputerState *s = XRCOMPUTER_MACHINE(hotplug_dev);
    MachineClass *mc = MACHINE_GET_CLASS(s);
    SysBusDevice *sbd;

    if (device_is_dynamic_sysbus(mc, dev)) {
        for (int i = 0; i < XRCOMPUTER_EBUS_COUNT; i++) {
            if (s->ebus[i] == NULL) {
                s->ebus[i] = dev;

                sbd = SYS_BUS_DEVICE(dev);

                if (sysbus_has_irq(sbd, 0)) {
                    sysbus_connect_irq(sbd, 0,
                        qdev_get_gpio_in(s->irqchip, EBUS_IRQ + i));
                }

                if (sysbus_has_mmio(sbd, 0)) {
                    sysbus_mmio_map(sbd, 0,
                        xrcomputer_memmap[XRCOMPUTER_EBUS].base
                            + xrcomputer_memmap[XRCOMPUTER_EBUS].size * i);
                }

                return;
            }
        }

        error_setg(errp, "XRcomputer: not enough EBus slots\n");
    }
}

static bool xrcomputer_get_headless(Object *obj, Error **errp)
{
    XRcomputerState *s = XRCOMPUTER_MACHINE(obj);
    return s->headless;
}

static void xrcomputer_set_headless(Object *obj, bool headless, Error **errp)
{
    XRcomputerState *s = XRCOMPUTER_MACHINE(obj);
    s->headless = headless;
}

static void xrcomputer_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->desc = "XR/computer board";
    mc->init = xrcomputer_init;
    mc->max_cpus = XRCOMPUTER_CPUS_MAX;
    mc->is_default = true;
    mc->default_cpu_type = TYPE_XR17032_CPU_BASE;
    mc->block_default_type = IF_XRDISK;
    mc->no_parallel = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->possible_cpu_arch_ids = xrcomputer_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = xrcomputer_cpu_index_to_props;
    mc->default_ram_id = "xrcomputer.ram";
    assert(!mc->get_hotplug_handler);
    mc->get_hotplug_handler = xrcomputer_get_hotplug_handler;

    hc->plug = xrcomputer_device_plug_cb;

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_EBUS_DEVICE);

    object_class_property_add_bool(oc, "headless", xrcomputer_get_headless,
        xrcomputer_set_headless);
}

static const TypeInfo xrcomputer_typeinfo = {
    .name       = TYPE_XRCOMPUTER_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = xrcomputer_class_init,
    .instance_size = sizeof(XRcomputerState),
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { }
    },
};

static void xrcomputer_init_register_types(void)
{
    type_register_static(&xrcomputer_typeinfo);
}

type_init(xrcomputer_init_register_types)

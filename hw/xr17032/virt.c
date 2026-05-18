/*
 * QEMU XR/17032 VirtIO Board
 *
 * Copyright (c) 2026 monkuous
 *
 * XR/17032 machine with 16550a UART and VirtIO MMIO
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
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qemu/guest-random.h"
#include "qapi/error.h"
#include "hw/core/boards.h"
#include "hw/core/loader.h"
#include "hw/core/sysbus.h"
#include "hw/core/qdev-properties.h"
#include "hw/char/serial-mm.h"
#include "target/xr17032/cpu.h"
#include "hw/core/sysbus-fdt.h"
#include "hw/xr17032/virt.h"
#include "hw/xr17032/boot.h"
#include "hw/intc/xrarch_lsic.h"
#include "hw/core/platform-bus.h"
#include "system/device_tree.h"
#include "system/system.h"
#include "system/kvm.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/gpex.h"
#include "hw/display/ramfb.h"
#include "system/reset.h"
#include "qemu/datadir.h"

#include <libfdt.h>

#define LSIC_SIZE 32
#define LSIC_SPACE 0x100000

#if VIRT_CPUS_MAX * LSIC_SIZE > LSIC_SPACE
#error "Too many CPUs, cannot place LSICs"
#endif

#if LSIC_SPACE > 0x8000000
#error "Too much space reserved for LSICs"
#endif

static const MemMapEntry virt_memmap[] = {
    [VIRT_DRAM] =         {        0x0,        0x0 },
    [VIRT_PCIE_MMIO] =    { 0xc0000000, 0x20000000 },
    [VIRT_PCIE_ECAM] =    { 0xe0000000, 0x10000000 },
    [VIRT_LSIC] =         { 0xf0000000, LSIC_SPACE },
    [VIRT_PLATFORM_BUS] = { 0xf8000000,  0x2000000 },
    [VIRT_PCIE_PIO] =     { 0xfa000000,    0x10000 },
    [VIRT_VIRTIO] =       { 0xfb000000,     0x1000 }, /* actually takes 0x8000 */
    [VIRT_UART0] =        { 0xfb010000,      0x100 },
    [VIRT_FW_CFG] =       { 0xfb020000,       0x18 },
    [VIRT_RTC] =          { 0xfb030000,        0x8 },
    [VIRT_FDT] =          { 0xfbf00000,   0x100000 },
    [VIRT_FLASH] =        { 0xfc000000,  0x4000000 },
};

#define VIRT_FLASH_SECTOR_SIZE (256 * KiB)

static PFlashCFI01 *virt_flash_create1(XR17032VirtState *s,
                                       const char *name,
                                       const char *alias_prop_name)
{
    /*
     * Create a single flash device.  We use the same parameters as
     * the flash devices on the ARM virt board.
     */
    DeviceState *dev = qdev_new(TYPE_PFLASH_CFI01);

    qdev_prop_set_uint64(dev, "sector-length", VIRT_FLASH_SECTOR_SIZE);
    qdev_prop_set_uint8(dev, "width", 4);
    qdev_prop_set_uint8(dev, "device-width", 2);
    qdev_prop_set_bit(dev, "big-endian", false);
    qdev_prop_set_uint16(dev, "id0", 0x89);
    qdev_prop_set_uint16(dev, "id1", 0x18);
    qdev_prop_set_uint16(dev, "id2", 0x00);
    qdev_prop_set_uint16(dev, "id3", 0x00);
    qdev_prop_set_string(dev, "name", name);

    object_property_add_child(OBJECT(s), name, OBJECT(dev));
    object_property_add_alias(OBJECT(s), alias_prop_name,
                              OBJECT(dev), "drive");

    return PFLASH_CFI01(dev);
}

static void virt_flash_create(XR17032VirtState *s)
{
    s->flash[0] = virt_flash_create1(s, "virt.flash0", "pflash0");
    s->flash[1] = virt_flash_create1(s, "virt.flash1", "pflash1");
}

static void virt_flash_map1(PFlashCFI01 *flash,
                            hwaddr base, hwaddr size,
                            MemoryRegion *sysmem)
{
    DeviceState *dev = DEVICE(flash);

    assert(QEMU_IS_ALIGNED(size, VIRT_FLASH_SECTOR_SIZE));
    assert(size / VIRT_FLASH_SECTOR_SIZE <= UINT32_MAX);
    qdev_prop_set_uint32(dev, "num-blocks", size / VIRT_FLASH_SECTOR_SIZE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    memory_region_add_subregion(sysmem, base,
                                sysbus_mmio_get_region(SYS_BUS_DEVICE(dev),
                                                       0));
}

static void virt_flash_map(XR17032VirtState *s,
                           MemoryRegion *sysmem)
{
    hwaddr flashsize = s->memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = s->memmap[VIRT_FLASH].base;

    virt_flash_map1(s->flash[1], flashbase, flashsize,
                    sysmem);
    virt_flash_map1(s->flash[0], flashbase + flashsize, flashsize,
                    sysmem);
}

static void create_pcie_irq_map(XR17032VirtState *s, void *fdt, char *nodename,
                                uint32_t irqchip_phandle)
{
    int pin, dev;
    uint32_t irq_map_stride = 0;
    uint32_t full_irq_map[PCI_NUM_PINS * PCI_NUM_PINS *
                          FDT_INT_MAP_WIDTH] = {};
    uint32_t *irq_map = full_irq_map;

    /* This code creates a standard swizzle of interrupts such that
     * each device's first interrupt is based on it's PCI_SLOT number.
     * (See pci_swizzle_map_irq_fn())
     *
     * We only need one entry per interrupt in the table (not one per
     * possible slot) seeing the interrupt-map-mask will allow the table
     * to wrap to any number of devices.
     */
    for (dev = 0; dev < PCI_NUM_PINS; dev++) {
        int devfn = dev * 0x8;

        for (pin = 0; pin < PCI_NUM_PINS; pin++) {
            int irq_nr = PCIE_IRQ + ((pin + PCI_SLOT(devfn)) % PCI_NUM_PINS);
            int i = 0;

            /* Fill PCI address cells */
            irq_map[i] = cpu_to_be32(devfn << 8);
            i += FDT_PCI_ADDR_CELLS;

            /* Fill PCI Interrupt cells */
            irq_map[i] = cpu_to_be32(pin + 1);
            i += FDT_PCI_INT_CELLS;

            /* Fill interrupt controller phandle and cells */
            irq_map[i++] = cpu_to_be32(irqchip_phandle);
            irq_map[i++] = cpu_to_be32(irq_nr);

            if (!irq_map_stride) {
                irq_map_stride = i;
            }
            irq_map += irq_map_stride;
        }
    }

    qemu_fdt_setprop(fdt, nodename, "interrupt-map", full_irq_map,
                     PCI_NUM_PINS * PCI_NUM_PINS *
                     irq_map_stride * sizeof(uint32_t));

    qemu_fdt_setprop_cells(fdt, nodename, "interrupt-map-mask",
                           0x1800, 0, 0, 0x7);
}

static void create_fdt_cpus(XR17032VirtState *s, uint32_t *phandle,
                            uint32_t *intc_phandles)
{
    int cpu;
    uint32_t cpu_phandle;
    MachineState *ms = MACHINE(s);
    XR17032CPU *cpu_ptr;
    char *nodename;

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        cpu_ptr = &s->cpus[cpu];

        nodename = g_strdup_printf("/cpus/cpu@%d", cpu);
        cpu_phandle = (*phandle)++;

        qemu_fdt_add_subnode(ms->fdt, nodename);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "phandle", cpu_phandle);
        qemu_fdt_setprop_string(ms->fdt, nodename, "device_type", "cpu");
        qemu_fdt_setprop_cell(ms->fdt, nodename, "reg", cpu);
        qemu_fdt_setprop_string(ms->fdt, nodename, "status", "okay");
        qemu_fdt_setprop_string(ms->fdt, nodename, "compatible", cpu_ptr->dtb_compatible);
        qemu_fdt_setprop(ms->fdt, nodename, "interrupt-controller", NULL, 0);
        qemu_fdt_setprop_cell(ms->fdt, nodename, "#interrupt-cells", 1);

        g_free(nodename);

        intc_phandles[cpu] = cpu_phandle;
    }
}

static void create_fdt_memory(XR17032VirtState *s)
{
    g_autofree char *mem_name = NULL;
    hwaddr addr;
    uint64_t size;
    MachineState *ms = MACHINE(s);

    addr = s->memmap[VIRT_DRAM].base;
    size = ms->ram_size;
    mem_name = g_strdup_printf("/memory@%"HWADDR_PRIx, addr);
    qemu_fdt_add_subnode(ms->fdt, mem_name);
    qemu_fdt_setprop_sized_cells(ms->fdt, mem_name, "reg", 2, addr, 2, size);
    qemu_fdt_setprop_string(ms->fdt, mem_name, "device_type", "memory");
}

static void create_fdt_lsic(XR17032VirtState *s, uint32_t *phandle,
                            uint32_t *intc_phandles, uint32_t *plic_phandle)
{
    int cpu;
    g_autofree char *lsic_name = NULL;
    g_autofree uint32_t *lsic_cells;
    unsigned long lsic_addr;
    MachineState *ms = MACHINE(s);

    *plic_phandle = (*phandle)++;
    lsic_addr = s->memmap[VIRT_LSIC].base;
    lsic_name = g_strdup_printf("/soc/lsic@%lx", lsic_addr);
    lsic_cells = g_new0(uint32_t, ms->smp.cpus);

    for (cpu = 0; cpu < ms->smp.cpus; cpu++) {
        lsic_cells[cpu * 2 + 0] = cpu_to_be32(intc_phandles[cpu]);
        lsic_cells[cpu * 2 + 1] = cpu_to_be32(EXCCODE_INT);
    }

    qemu_fdt_add_subnode(ms->fdt, lsic_name);
    qemu_fdt_setprop_cell(ms->fdt, lsic_name, "phandle", *plic_phandle);
    qemu_fdt_setprop_sized_cells(ms->fdt, lsic_name, "reg", 2, lsic_addr,
        2, ms->smp.cpus * XRARCH_LSIC_STRIDE);
    qemu_fdt_setprop_string(ms->fdt, lsic_name, "compatible", "xrarch,lsic");
    qemu_fdt_setprop(ms->fdt, lsic_name, "interrupts-extended", lsic_cells,
                     ms->smp.cpus * sizeof(uint32_t) * 2);
    qemu_fdt_setprop(ms->fdt, lsic_name, "interrupt-controller", NULL, 0);
    qemu_fdt_setprop_cell(ms->fdt, lsic_name, "#interrupt-cells",
        FDT_INT_CELLS);

    platform_bus_add_all_fdt_nodes(ms->fdt, lsic_name,
                                   s->memmap[VIRT_PLATFORM_BUS].base,
                                   s->memmap[VIRT_PLATFORM_BUS].size,
                                   VIRT_PLATFORM_BUS_IRQ);
}

static void create_fdt_rtc(XR17032VirtState *s, uint32_t irq_phandle)
{
    g_autofree char *rtc_name = NULL;
    unsigned long rtc_addr;
    MachineState *ms = MACHINE(s);

    rtc_addr = s->memmap[VIRT_RTC].base;
    rtc_name = g_strdup_printf("/soc/rtc@%lx", rtc_addr);

    qemu_fdt_add_subnode(ms->fdt, rtc_name);
    qemu_fdt_setprop_sized_cells(ms->fdt, rtc_name, "reg", 2, rtc_addr,
        2, s->memmap[VIRT_RTC].size);
    qemu_fdt_setprop_string(ms->fdt, rtc_name, "compatible", "xrarch,rtc");
    qemu_fdt_setprop_cell(ms->fdt, rtc_name, "interrupt-parent", irq_phandle);
    qemu_fdt_setprop_cell(ms->fdt, rtc_name, "interrupts", RTC_IRQ);
}

static void create_fdt_platform(XR17032VirtState *s, uint32_t *phandle,
                                uint32_t *irq_phandle)
{
    MachineState *ms = MACHINE(s);
    g_autofree uint32_t *intc_phandles = NULL;

    qemu_fdt_add_subnode(ms->fdt, "/cpus");
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#size-cells", 0x0);
    qemu_fdt_setprop_cell(ms->fdt, "/cpus", "#address-cells", 0x1);

    intc_phandles = g_new0(uint32_t, ms->smp.cpus);

    create_fdt_cpus(s, phandle, intc_phandles);
    create_fdt_memory(s);
    create_fdt_lsic(s, phandle, intc_phandles, irq_phandle);
    create_fdt_rtc(s, *irq_phandle);
}

static void create_fdt_virtio(XR17032VirtState *s, uint32_t irq_phandle)
{
    int i;
    MachineState *ms = MACHINE(s);
    hwaddr virtio_base = s->memmap[VIRT_VIRTIO].base;

    for (i = 0; i < VIRTIO_COUNT; i++) {
        g_autofree char *name = NULL;
        uint64_t size = s->memmap[VIRT_VIRTIO].size;
        hwaddr addr = virtio_base + i * size;

        name = g_strdup_printf("/soc/virtio_mmio@%"HWADDR_PRIx, addr);

        qemu_fdt_add_subnode(ms->fdt, name);
        qemu_fdt_setprop_string(ms->fdt, name, "compatible", "virtio,mmio");
        qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2, addr, 2, size);
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent", irq_phandle);
        qemu_fdt_setprop_cell(ms->fdt, name, "interrupts", VIRTIO_IRQ + i);
    }
}

static void create_fdt_pcie(XR17032VirtState *s, uint32_t irq_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/pci@%"HWADDR_PRIx,
                           s->memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_setprop_cell(ms->fdt, name, "#address-cells",
        FDT_PCI_ADDR_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, name, "#interrupt-cells",
        FDT_PCI_INT_CELLS);
    qemu_fdt_setprop_cell(ms->fdt, name, "#size-cells", 2);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible",
        "pci-host-ecam-generic");
    qemu_fdt_setprop_string(ms->fdt, name, "device_type", "pci");
    qemu_fdt_setprop_cell(ms->fdt, name, "linux,pci-domain", 0);
    qemu_fdt_setprop_cells(ms->fdt, name, "bus-range", 0,
        s->memmap[VIRT_PCIE_ECAM].size / PCIE_MMCFG_SIZE_MIN - 1);
    qemu_fdt_setprop(ms->fdt, name, "dma-coherent", NULL, 0);
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg", 2,
        s->memmap[VIRT_PCIE_ECAM].base, 2, s->memmap[VIRT_PCIE_ECAM].size);
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "ranges",
        1, FDT_PCI_RANGE_IOPORT, 2, 0,
        2, s->memmap[VIRT_PCIE_PIO].base, 2, s->memmap[VIRT_PCIE_PIO].size,
        1, FDT_PCI_RANGE_MMIO,
        2, s->memmap[VIRT_PCIE_MMIO].base,
        2, s->memmap[VIRT_PCIE_MMIO].base, 2, s->memmap[VIRT_PCIE_MMIO].size);

    create_pcie_irq_map(s, ms->fdt, name, irq_phandle);
}

static void create_fdt_uart(XR17032VirtState *s, uint32_t irq_phandle)
{
    g_autofree char *name = NULL;
    MachineState *ms = MACHINE(s);

    name = g_strdup_printf("/soc/serial@%"HWADDR_PRIx,
                           s->memmap[VIRT_UART0].base);
    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "ns16550a");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                 2, s->memmap[VIRT_UART0].base,
                                 2, s->memmap[VIRT_UART0].size);
    qemu_fdt_setprop_cell(ms->fdt, name, "clock-frequency", 3686400);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupt-parent", irq_phandle);
    qemu_fdt_setprop_cell(ms->fdt, name, "interrupts", UART0_IRQ);

    qemu_fdt_setprop_string(ms->fdt, "/chosen", "stdout-path", name);
    qemu_fdt_setprop_string(ms->fdt, "/aliases", "serial0", name);
}

static void create_fdt_flash(XR17032VirtState *s)
{
    MachineState *ms = MACHINE(s);
    hwaddr flashsize = s->memmap[VIRT_FLASH].size / 2;
    hwaddr flashbase = s->memmap[VIRT_FLASH].base;
    g_autofree char *name = g_strdup_printf("/flash@%" PRIx64, flashbase);

    qemu_fdt_add_subnode(ms->fdt, name);
    qemu_fdt_setprop_string(ms->fdt, name, "compatible", "cfi-flash");
    qemu_fdt_setprop_sized_cells(ms->fdt, name, "reg",
                                 2, flashbase + flashsize, 2, flashsize,
                                 2, flashbase, 2, flashsize);
    qemu_fdt_setprop_cell(ms->fdt, name, "bank-width", 4);
}

static void create_fdt_fw_cfg(XR17032VirtState *s)
{
    MachineState *ms = MACHINE(s);
    hwaddr base = s->memmap[VIRT_FW_CFG].base;
    hwaddr size = s->memmap[VIRT_FW_CFG].size;
    g_autofree char *nodename = g_strdup_printf("/fw-cfg@%" PRIx64, base);

    qemu_fdt_add_subnode(ms->fdt, nodename);
    qemu_fdt_setprop_string(ms->fdt, nodename,
                            "compatible", "qemu,fw-cfg-mmio");
    qemu_fdt_setprop_sized_cells(ms->fdt, nodename, "reg",
                                 2, base, 2, size);
    qemu_fdt_setprop(ms->fdt, nodename, "dma-coherent", NULL, 0);
}

static void finalize_fdt(XR17032VirtState *s)
{
    uint32_t phandle = 1, irq_phandle = 1;

    create_fdt_platform(s, &phandle, &irq_phandle);

    create_fdt_virtio(s, irq_phandle);

    create_fdt_pcie(s, irq_phandle);

    create_fdt_uart(s, irq_phandle);
}

static void create_fdt(XR17032VirtState *s)
{
    MachineState *ms = MACHINE(s);
    uint8_t rng_seed[32];
    g_autofree char *name = NULL;

    ms->fdt = create_device_tree(&s->fdt_size);
    if (!ms->fdt) {
        error_report("create_device_tree() failed");
        exit(1);
    }

    qemu_fdt_setprop_string(ms->fdt, "/", "model", "xrarch-virtio,qemu");
    qemu_fdt_setprop_string(ms->fdt, "/", "compatible", "xrarch-virtio");
    qemu_fdt_setprop_cell(ms->fdt, "/", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/", "#address-cells", 0x2);

    qemu_fdt_add_subnode(ms->fdt, "/soc");
    qemu_fdt_setprop(ms->fdt, "/soc", "ranges", NULL, 0);
    qemu_fdt_setprop_string(ms->fdt, "/soc", "compatible", "simple-bus");
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "#size-cells", 0x2);
    qemu_fdt_setprop_cell(ms->fdt, "/soc", "#address-cells", 0x2);

    /*
     * The "/soc/pci@..." node is needed for PCIE hotplugs
     * that might happen before finalize_fdt().
     */
    name = g_strdup_printf("/soc/pci@%"HWADDR_PRIx,
                           s->memmap[VIRT_PCIE_ECAM].base);
    qemu_fdt_add_subnode(ms->fdt, name);

    qemu_fdt_add_subnode(ms->fdt, "/chosen");

    /* Pass seed to RNG */
    qemu_guest_getrandom_nofail(rng_seed, sizeof(rng_seed));
    qemu_fdt_setprop(ms->fdt, "/chosen", "rng-seed",
                     rng_seed, sizeof(rng_seed));

    qemu_fdt_add_subnode(ms->fdt, "/aliases");

    create_fdt_flash(s);
    create_fdt_fw_cfg(s);
}

static inline DeviceState *gpex_pcie_init(MemoryRegion *sys_mem,
                                          DeviceState *irqchip,
                                          XR17032VirtState *s)
{
    DeviceState *dev;
    MemoryRegion *ecam_alias, *ecam_reg;
    MemoryRegion *mmio_alias, *mmio_reg;
    hwaddr ecam_base = s->memmap[VIRT_PCIE_ECAM].base;
    hwaddr ecam_size = s->memmap[VIRT_PCIE_ECAM].size;
    hwaddr mmio_base = s->memmap[VIRT_PCIE_MMIO].base;
    hwaddr mmio_size = s->memmap[VIRT_PCIE_MMIO].size;
    hwaddr pio_base = s->memmap[VIRT_PCIE_PIO].base;
    hwaddr pio_size = s->memmap[VIRT_PCIE_PIO].size;
    qemu_irq irq;
    int i;

    dev = qdev_new(TYPE_GPEX_HOST);

    /* Set GPEX object properties for the virt machine */
    object_property_set_uint(OBJECT(dev), PCI_HOST_ECAM_BASE,
                            ecam_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_ECAM_SIZE,
                            ecam_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_BELOW_4G_MMIO_BASE,
                             mmio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_BELOW_4G_MMIO_SIZE,
                            mmio_size, NULL);
    object_property_set_uint(OBJECT(dev), PCI_HOST_PIO_BASE,
                            pio_base, NULL);
    object_property_set_int(OBJECT(dev), PCI_HOST_PIO_SIZE,
                            pio_size, NULL);

    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    ecam_alias = g_new0(MemoryRegion, 1);
    ecam_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 0);
    memory_region_init_alias(ecam_alias, OBJECT(dev), "pcie-ecam",
                             ecam_reg, 0, ecam_size);
    memory_region_add_subregion(get_system_memory(), ecam_base, ecam_alias);

    mmio_alias = g_new0(MemoryRegion, 1);
    mmio_reg = sysbus_mmio_get_region(SYS_BUS_DEVICE(dev), 1);
    memory_region_init_alias(mmio_alias, OBJECT(dev), "pcie-mmio",
                             mmio_reg, mmio_base, mmio_size);
    memory_region_add_subregion(get_system_memory(), mmio_base, mmio_alias);

    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, pio_base);

    for (i = 0; i < PCI_NUM_PINS; i++) {
        irq = qdev_get_gpio_in(irqchip, PCIE_IRQ + i);

        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq);
        gpex_set_irq_num(GPEX_HOST(dev), i, PCIE_IRQ + i);
    }

    GPEX_HOST(dev)->gpex_cfg.bus = PCI_HOST_BRIDGE(dev)->bus;
    return dev;
}

static FWCfgState *create_fw_cfg(const MachineState *ms, hwaddr base)
{
    FWCfgState *fw_cfg;

    fw_cfg = fw_cfg_init_mem_dma(base + 8, base, 8, base + 16,
                                 &address_space_memory);
    fw_cfg_add_i16(fw_cfg, FW_CFG_NB_CPUS, (uint16_t)ms->smp.cpus);

    return fw_cfg;
}

static DeviceState *virt_create_lsic(const MemMapEntry *memmap, int hart_count)
{
    return xrarch_lsic_create(memmap[VIRT_LSIC].base, hart_count);
}

static void create_platform_bus(XR17032VirtState *s, DeviceState *irqchip)
{
    DeviceState *dev;
    SysBusDevice *sysbus;
    int i;
    MemoryRegion *sysmem = get_system_memory();

    dev = qdev_new(TYPE_PLATFORM_BUS_DEVICE);
    dev->id = g_strdup(TYPE_PLATFORM_BUS_DEVICE);
    qdev_prop_set_uint32(dev, "num_irqs", VIRT_PLATFORM_BUS_NUM_IRQS);
    qdev_prop_set_uint32(dev, "mmio_size", s->memmap[VIRT_PLATFORM_BUS].size);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    s->platform_bus_dev = dev;

    sysbus = SYS_BUS_DEVICE(dev);
    for (i = 0; i < VIRT_PLATFORM_BUS_NUM_IRQS; i++) {
        int irq = VIRT_PLATFORM_BUS_IRQ + i;
        sysbus_connect_irq(sysbus, i, qdev_get_gpio_in(irqchip, irq));
    }

    memory_region_add_subregion(sysmem,
                                s->memmap[VIRT_PLATFORM_BUS].base,
                                sysbus_mmio_get_region(sysbus, 0));
}

static void virt_machine_done(Notifier *notifier, void *data)
{
    XR17032VirtState *s = container_of(notifier, XR17032VirtState,
                                       machine_done);
    MachineState *machine = MACHINE(s);
    const char *firmware_name = machine->firmware;
    char *bios_name;
    BlockBackend *pflash_blk0;
    MemoryRegion *mr;
    ssize_t bios_size;
    hwaddr fdtaddr;
    uint32_t fdtsize;
    int ret;

    /*
     * An user provided dtb must include everything, including
     * dynamic sysbus devices. Our FDT needs to be finalized.
     */
    if (machine->dtb == NULL) {
        finalize_fdt(s);
    }

    pflash_blk0 = pflash_cfi01_get_blk(s->flash[0]);

    if (pflash_blk0) {
        if (firmware_name) {
            error_report("cannot use both '-bios' and '-drive if=pflash'"
                         "options at once");
            exit(1);
        }
        s->bios_loaded = true;
    } else if (firmware_name) {
        bios_name = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware_name);
        if (!bios_name) {
            error_report("Could not find ROM image '%s'", firmware_name);
            exit(1);
        }

        mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(s->flash[0]), 0);
        bios_size = load_image_mr(bios_name, mr);
        if (bios_size < 0) {
            error_report("Could not load ROM image '%s'", bios_name);
            exit(1);
        }
        g_free(bios_name);
        s->bios_loaded = true;
    }

    xr17032_load_kernel(machine);

    ret = fdt_pack(machine->fdt);
    g_assert(ret == 0);

    fdtsize = fdt_totalsize(machine->fdt);
    g_assert(fdtsize <= virt_memmap[VIRT_FDT].size);
    fdtaddr = virt_memmap[VIRT_FDT].base;
    rom_add_blob_fixed_as("fdt", machine->fdt, fdtsize, fdtaddr,
                          &address_space_memory);
    qemu_register_reset_nosnapshotload(qemu_fdt_randomize_seeds, 
        rom_ptr_for_as(&address_space_memory, fdtaddr, fdtsize));
}

static void xr17032_cpus_reset(void *opaque)
{
    XR17032CPU *cpu = opaque;
    cpu_reset(CPU(cpu));
}

static void virt_machine_init(MachineState *machine)
{
    XR17032VirtState *s = XR17032_VIRT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *fdt_rom = g_new(MemoryRegion, 1);
    int i;

#if HOST_LONG_BITS == 64
    /* limit RAM size in a 32-bit system */
    if (machine->ram_size > 3 * GiB) {
        machine->ram_size = 3 * GiB;
        error_report("Limiting RAM size to 3 GiB");
    }
#endif

    s->memmap = virt_memmap;

    s->cpus = g_new0(XR17032CPU, machine->smp.cpus);
    for (i = 0; i < machine->smp.cpus; i++) {
        object_initialize_child(OBJECT(machine), "cpus[*]", &s->cpus[i], machine->cpu_type);

        s->cpus[i].phy_id = i;
        qemu_register_reset(xr17032_cpus_reset, CPU(&s->cpus[i]));

        if (!qdev_realize(DEVICE(&s->cpus[i]), NULL, &error_fatal)) {
            return;
        }
    }

    /* Initialize irqchip */
    s->irqchip = virt_create_lsic(s->memmap, machine->smp.cpus);

    /* Initialize rtc */
    sysbus_create_simple("xrarch_rtc", s->memmap[VIRT_RTC].base,
        qdev_get_gpio_in(s->irqchip, RTC_IRQ));

    /* register system main memory (actual RAM) */
    memory_region_add_subregion(system_memory, s->memmap[VIRT_DRAM].base,
                                machine->ram);

    /* add fdt rom region */
    memory_region_init_rom(fdt_rom, NULL, "xr17032_virt_board.fdt",
                           s->memmap[VIRT_FDT].size, &error_fatal);
    memory_region_add_subregion(system_memory, s->memmap[VIRT_FDT].base,
                                fdt_rom);

    /*
     * Init fw_cfg. Must be done before xr17032_load_fdt, otherwise the
     * device tree cannot be altered and we get FDT_ERR_NOSPACE.
     */
    s->fw_cfg = create_fw_cfg(machine, s->memmap[VIRT_FW_CFG].base);
    rom_set_fw(s->fw_cfg);

    /* VirtIO MMIO devices */
    for (i = 0; i < VIRTIO_COUNT; i++) {
        sysbus_create_simple("virtio-mmio",
            s->memmap[VIRT_VIRTIO].base + i * s->memmap[VIRT_VIRTIO].size,
            qdev_get_gpio_in(s->irqchip, VIRTIO_IRQ + i));
    }

    gpex_pcie_init(system_memory, s->irqchip, s);

    create_platform_bus(s, s->irqchip);

    serial_mm_init(system_memory, s->memmap[VIRT_UART0].base,
        0, qdev_get_gpio_in(s->irqchip, UART0_IRQ), 399193,
        serial_hd(0), DEVICE_LITTLE_ENDIAN);

    for (i = 0; i < ARRAY_SIZE(s->flash); i++) {
        /* Map legacy -drive if=pflash to machine properties */
        pflash_cfi01_legacy_drive(s->flash[i],
                                  drive_get(IF_PFLASH, 0, i));
    }
    virt_flash_map(s, system_memory);

    /* load/create device tree */
    if (machine->dtb) {
        machine->fdt = load_device_tree(machine->dtb, &s->fdt_size);
        if (!machine->fdt) {
            error_report("load_device_tree() failed");
            exit(1);
        }
    } else {
        create_fdt(s);
    }

    s->machine_done.notify = virt_machine_done;
    qemu_add_machine_init_done_notifier(&s->machine_done);
}

static void virt_machine_instance_init(Object *obj)
{
    XR17032VirtState *s = XR17032_VIRT_MACHINE(obj);

    virt_flash_create(s);
}

static void virt_machine_device_plug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    XR17032VirtState *s = XR17032_VIRT_MACHINE(hotplug_dev);

    if (s->platform_bus_dev) {
        MachineClass *mc = MACHINE_GET_CLASS(s);

        if (device_is_dynamic_sysbus(mc, dev)) {
            platform_bus_link_device(PLATFORM_BUS_DEVICE(s->platform_bus_dev),
                                     SYS_BUS_DEVICE(dev));
        }
    }
}

static const CPUArchIdList *virt_possible_cpu_arch_ids(MachineState *ms)
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
        ms->possible_cpus->cpus[n].vcpus_count = 1;
    }
    return ms->possible_cpus;
}

static CpuInstanceProperties virt_cpu_index_to_props(MachineState *ms,
                                                     unsigned cpu_index)
{
    MachineClass *mc = MACHINE_GET_CLASS(ms);
    const CPUArchIdList *possible_cpus = mc->possible_cpu_arch_ids(ms);

    assert(cpu_index < possible_cpus->len);
    return possible_cpus->cpus[cpu_index].props;
}

static void virt_machine_class_init(ObjectClass *oc, const void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(oc);

    mc->desc = "XR/17032 VirtIO board";
    mc->init = virt_machine_init;
    mc->max_cpus = VIRT_CPUS_MAX;
    mc->is_default = true;
    mc->default_cpu_type = TYPE_XR17032_CPU_BASE;
    mc->block_default_type = IF_VIRTIO;
    mc->no_cdrom = 1;
    mc->pci_allow_0_address = true;
    mc->possible_cpu_arch_ids = virt_possible_cpu_arch_ids;
    mc->cpu_index_to_instance_props = virt_cpu_index_to_props;
    /* platform instead of architectural choice */
    mc->cpu_cluster_has_numa_boundary = true;
    mc->default_ram_id = "xr17032_virt_board.ram";
    assert(!mc->get_hotplug_handler);

    hc->plug = virt_machine_device_plug_cb;

    machine_class_allow_dynamic_sysbus_dev(mc, TYPE_RAMFB_DEVICE);
}

static const TypeInfo virt_machine_typeinfo = {
    .name       = MACHINE_TYPE_NAME("virt"),
    .parent     = TYPE_MACHINE,
    .class_init = virt_machine_class_init,
    .instance_init = virt_machine_instance_init,
    .instance_size = sizeof(XR17032VirtState),
    .interfaces = (const InterfaceInfo[]) {
         { TYPE_HOTPLUG_HANDLER },
         { }
    },
};

static void virt_machine_init_register_types(void)
{
    type_register_static(&virt_machine_typeinfo);
}

type_init(virt_machine_init_register_types)

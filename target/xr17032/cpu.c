/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU XR/17032 CPU
 *
 * Copyright (c) 2026 monkuous
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/qemu-print.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "system/qtest.h"
#include "system/tcg.h"
#include "system/kvm.h"
#include "hw/core/qdev-properties.h"
#include "exec/translation-block.h"
#include "cpu.h"
#include "cpu-mmu.h"
#include "internals.h"
#include "fpu/softfloat-helpers.h"
#include "cr.h"
#ifndef CONFIG_USER_ONLY
#include "system/reset.h"
#endif
#include "tcg/tcg_xr17032.h"

/* 16 bytes per cache line, 1-way associative, 2048 lines */
#define CACHECTRL_VALUE 0x04010b

const char * const regnames[32] = {
    "zero", "t0", "t1", "t2", "t3", "t4", "t5", "a0",
    "a1", "a2", "a3", "s0", "s1", "s2", "s3", "s4",
    "s5", "s6", "s7", "s8", "s9", "s10", "s11", "s12",
    "s13", "s14", "s15", "s16", "s17", "tp", "sp", "lr",
};

static void xr17032_cpu_set_pc(CPUState *cs, vaddr value)
{
    cpu_env(cs)->pc = value;
}

static vaddr xr17032_cpu_get_pc(CPUState *cs)
{
    return cpu_env(cs)->pc;
}

#ifndef CONFIG_USER_ONLY
void xr17032_cpu_set_irq(void *opaque, int irq, int level)
{
    XR17032CPU *cpu = opaque;
    CPUState *cs = CPU(cpu);

    if (irq != 0) {
        return;
    }

    if (tcg_enabled()) {
        if (level) {
            cpu_interrupt(cs, CPU_INTERRUPT_HARD);
        } else {
            cpu_reset_interrupt(cs, CPU_INTERRUPT_HARD);
        }
    }
}
#endif

#ifndef CONFIG_USER_ONLY
bool xr17032_cpu_has_work(CPUState *cs)
{
    bool has_work = false;

    if (cpu_test_interrupt(cs, CPU_INTERRUPT_HARD)) {
        has_work = true;
    }

    return has_work;
}
#endif /* !CONFIG_USER_ONLY */

static void xr17032_xr17032_initfn(Object *obj)
{
    XR17032CPU *cpu = XR17032_CPU(obj);

    cpu->dtb_compatible = "xrarch,xr17032";
}

static void xr17032_cpu_reset_hold(Object *obj, ResetType type)
{
    CPUState *cs = CPU(obj);
    XR17032CPUClass *xrcc = XR17032_CPU_GET_CLASS(obj);
    CPUXR17032State *env = cpu_env(cs);

    if (xrcc->parent_phases.hold) {
        xrcc->parent_phases.hold(obj, type);
    }

    /* Set cr registers value after reset, see the manual 1.2. */
    env->CR_RS = 0;
    env->CR_WHAMI = XR17032_CPU(obj)->phy_id;
    env->CR_EB = 0;
    env->CR_ICACHECTRL = CACHECTRL_VALUE;
    env->CR_DCACHECTRL = CACHECTRL_VALUE;

#ifndef CONFIG_USER_ONLY
    env->pc = 0xfffe1000;
#endif

    cs->exception_index = -1;
}

static void xr17032_cpu_disas_set_info(const CPUState *cs,
                                       disassemble_info *info)
{
    info->endian = BFD_ENDIAN_LITTLE;
    info->print_insn = print_insn_xr17032;
}

static void xr17032_cpu_realizefn(DeviceState *dev, Error **errp)
{
    CPUState *cs = CPU(dev);
    XR17032CPUClass *xrcc = XR17032_CPU_GET_CLASS(dev);
    Error *local_err = NULL;

    cpu_exec_realizefn(cs, &local_err);
    if (local_err != NULL) {
        error_propagate(errp, local_err);
        return;
    }

    qemu_init_vcpu(cs);
    cpu_reset(cs);

    xrcc->parent_realize(dev, errp);
}

static void xr17032_cpu_unrealizefn(DeviceState *dev)
{
    XR17032CPUClass *xrcc = XR17032_CPU_GET_CLASS(dev);

#ifndef CONFIG_USER_ONLY
    cpu_remove_sync(CPU(dev));
#endif

    xrcc->parent_unrealize(dev);
}

static void xr17032_cpu_init(Object *obj)
{
#ifndef CONFIG_USER_ONLY
    XR17032CPU *cpu = XR17032_CPU(obj);

    qdev_init_gpio_in(DEVICE(cpu), xr17032_cpu_set_irq, 1);
#endif
}

static ObjectClass *xr17032_cpu_class_by_name(const char *cpu_model)
{
    ObjectClass *oc;

    oc = object_class_by_name(cpu_model);
    if (!oc) {
        g_autofree char *typename
            = g_strdup_printf(XR17032_CPU_TYPE_NAME("%s"), cpu_model);
        oc = object_class_by_name(typename);
    }

    return oc;
}

static void xr17032_cpu_dump_cr(CPUState *cs, FILE *f)
{
#ifndef CONFIG_USER_ONLY
    CPUXR17032State *env = cpu_env(cs);
    CRInfo *cr_info;
    int32_t *addr;
    int i, j, col = 0;

    qemu_fprintf(f, "\n");

    /* Dump all generic CR register */
    for (i = 0; i < 32; i++) {
        cr_info = get_cr(i);
        if (!cr_info) {
            if (i == (col + 3)) {
                qemu_fprintf(f, "\n");
            }

            continue;
        }

        if ((i >  (col + 3)) || (i == col)) {
            col = i & ~3;
            qemu_fprintf(f, " CR%02d:", col);

            for (j = col; j < i; j++) {
                qemu_fprintf(f, "                    ");
            }
        }

        addr = (void *)env + cr_info->offset;
        qemu_fprintf(f, " %10s %08x", cr_info->name, *addr);

        if (i == (col + 3)) {
            qemu_fprintf(f, "\n");
        }
    }
    qemu_fprintf(f, "\n");
#endif
}

static void xr17032_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    CPUXR17032State *env = cpu_env(cs);
    int i;

    qemu_fprintf(f, " PC=%08x\n", env->pc);

    /* gpr */
    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0) {
            qemu_fprintf(f, " GPR%02d:", i);
        }
        qemu_fprintf(f, " %4s %08x", regnames[i], env->gpr[i]);
        if ((i & 3) == 3) {
            qemu_fprintf(f, "\n");
        }
    }

    /* cr */
    xr17032_cpu_dump_cr(cs, f);
}

#ifndef CONFIG_USER_ONLY
#include "hw/core/sysemu-cpu-ops.h"

static const struct SysemuCPUOps xr17032_sysemu_ops = {
    .has_work = xr17032_cpu_has_work,
    .write_elf32_note = xr17032_cpu_write_elf32_note,
    .get_phys_page_debug = xr17032_cpu_get_phys_page_debug,
};

static int64_t xr17032_cpu_get_arch_id(CPUState *cs)
{
    XR17032CPU *cpu = XR17032_CPU(cs);

    return cpu->phy_id;
}
#endif

static const Property xr17032_cpu_properties[] = {
    DEFINE_PROP_INT32("socket-id", XR17032CPU, socket_id, 0),
    DEFINE_PROP_INT32("node-id", XR17032CPU, node_id, CPU_UNSET_NUMA_NODE_ID),
};

static const gchar *xr17032_gdb_arch_name(CPUState *cs)
{
    return "xr17032";
}

static void xr17032_cpu_class_init(ObjectClass *c, const void *data)
{
    XR17032CPUClass *xrcc = XR17032_CPU_CLASS(c);
    CPUClass *cc = CPU_CLASS(c);
    DeviceClass *dc = DEVICE_CLASS(c);
    ResettableClass *rc = RESETTABLE_CLASS(c);

    device_class_set_props(dc, xr17032_cpu_properties);
    device_class_set_parent_realize(dc, xr17032_cpu_realizefn,
                                    &xrcc->parent_realize);
    device_class_set_parent_unrealize(dc, xr17032_cpu_unrealizefn,
                                      &xrcc->parent_unrealize);
    resettable_class_set_parent_phases(rc, NULL, xr17032_cpu_reset_hold, NULL,
                                       &xrcc->parent_phases);

    cc->class_by_name = xr17032_cpu_class_by_name;
    cc->dump_state = xr17032_cpu_dump_state;
    cc->set_pc = xr17032_cpu_set_pc;
    cc->get_pc = xr17032_cpu_get_pc;
#ifndef CONFIG_USER_ONLY
    cc->get_arch_id = xr17032_cpu_get_arch_id;
    dc->vmsd = &vmstate_xr17032_cpu;
    cc->sysemu_ops = &xr17032_sysemu_ops;
#endif
    cc->disas_set_info = xr17032_cpu_disas_set_info;
    cc->gdb_read_register = xr17032_cpu_gdb_read_register;
    cc->gdb_write_register = xr17032_cpu_gdb_write_register;
    cc->gdb_stop_before_watchpoint = true;

#ifdef CONFIG_TCG
    cc->tcg_ops = &xr17032_tcg_ops;
#endif
    dc->user_creatable = true;

    cc->gdb_core_xml_file = "xr17032-base.xml";
    cc->gdb_arch_name = xr17032_gdb_arch_name;
}

#define DEFINE_XR17032_CPU_TYPE(model, initfn) \
    { \
        .parent = TYPE_XR17032_CPU, \
        .instance_init = initfn, \
        .name = model, \
    }

static const TypeInfo xr17032_cpu_type_infos[] = {
    {
        .name = TYPE_XR17032_CPU,
        .parent = TYPE_CPU,
        .instance_size = sizeof(XR17032CPU),
        .instance_align = __alignof(XR17032CPU),
        .instance_init = xr17032_cpu_init,

        .abstract = true,
        .class_size = sizeof(XR17032CPUClass),
        .class_init = xr17032_cpu_class_init,
    },
    DEFINE_XR17032_CPU_TYPE(TYPE_XR17032_CPU_BASE, xr17032_xr17032_initfn),
};

DEFINE_TYPES(xr17032_cpu_type_infos)

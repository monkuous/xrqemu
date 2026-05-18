/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU XR/17032 CPU -- internal functions and types
 *
 * Copyright (c) 2026 monkuous
 */

#ifndef XR17032_INTERNALS_H
#define XR17032_INTERNALS_H

#define TARGET_VIRT_MASK MAKE_64BIT_MASK(0, TARGET_VIRT_ADDR_SPACE_BITS)

void xr17032_translate_init(void);
void xr17032_translate_code(CPUState *cs, TranslationBlock *tb,
                            int *max_insns, vaddr pc, void *host_pc);

void G_NORETURN do_raise_exception(CPUXR17032State *env,
                                   uint32_t exception,
                                   uintptr_t pc);

#ifndef CONFIG_USER_ONLY
extern const VMStateDescription vmstate_xr17032_cpu;

void xr17032_cpu_set_irq(void *opaque, int irq, int level);

bool xr17032_cpu_has_work(CPUState *cs);
#endif /* !CONFIG_USER_ONLY */

int xr17032_cpu_gdb_read_register(CPUState *cs, GByteArray *mem_buf, int n);
int xr17032_cpu_gdb_write_register(CPUState *cs, uint8_t *mem_buf, int n);
int xr17032_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cpu,
                                 int cpuid, DumpState *s);

#endif

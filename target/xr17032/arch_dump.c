/*
 * Support for writing ELF notes for XR/17032 architectures
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
 *
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "elf.h"
#include "system/dump.h"
#include "internals.h"

/* struct user_pt_regs from arch/xr17032/include/uapi/asm/ptrace.h */
struct xr17032_user_regs {
    uint32_t pc;
    uint32_t gpr[31];
} QEMU_PACKED;

QEMU_BUILD_BUG_ON(sizeof(struct xr17032_user_regs) != 128);

/* struct elf_prstatus from include/uapi/linux/elfcore.h */
struct xr17032_elf_prstatus {
    char pad1[24]; /* 24 == offsetof(struct elf_prstatus, pr_pid) */
    uint32_t pr_pid;
    /*
     * 44 == offsetof(struct elf_prstatus, pr_reg) -
     * offsetof(struct elf_prstatus, pr_ppid)
     */
    char pad2[44];
    struct xr17032_user_regs pr_reg;
    uint32_t pr_fpvalid;
} QEMU_PACKED;

QEMU_BUILD_BUG_ON(sizeof(struct xr17032_elf_prstatus) != 204);

struct xr17032_note {
    Elf32_Nhdr hdr;
    char name[8]; /* align_up(sizeof("CORE"), 4) */
    struct xr17032_elf_prstatus prstatus;
} QEMU_PACKED;

#define XR17032_NOTE_HEADER_SIZE offsetof(struct xr17032_note, prstatus)
#define XR17032_PRSTATUS_NOTE_SIZE                                          \
    (XR17032_NOTE_HEADER_SIZE + sizeof(struct xr17032_elf_prstatus))

static void xr17032_note_init(struct xr17032_note *note, DumpState *s,
                              const char *name, Elf32_Word namesz,
                              Elf32_Word type, Elf32_Word descsz)
{
    memset(note, 0, sizeof(*note));

    note->hdr.n_namesz = cpu_to_dump32(s, namesz);
    note->hdr.n_descsz = cpu_to_dump32(s, descsz);
    note->hdr.n_type = cpu_to_dump32(s, type);

    memcpy(note->name, name, namesz);
}

int xr17032_cpu_write_elf32_note(WriteCoreDumpFunction f, CPUState *cs,
                                 int cpuid, DumpState *s)
{
    struct xr17032_note note;
    CPUXR17032State *env = &XR17032_CPU(cs)->env;
    int ret, i;

    xr17032_note_init(&note, s, "CORE", 5, NT_PRSTATUS,
                      sizeof(note.prstatus));
    note.prstatus.pr_pid = cpu_to_dump32(s, cpuid);
    note.prstatus.pr_fpvalid = 0;

    note.prstatus.pr_reg.pc = cpu_to_dump32(s, env->pc);

    for (i = 0; i < 31; ++i) {
        note.prstatus.pr_reg.gpr[i] = cpu_to_dump32(s, env->gpr[i + 1]);
    }

    ret = f(&note, XR17032_PRSTATUS_NOTE_SIZE, s);
    if (ret < 0) {
        return -1;
    }

    return ret;
}

int cpu_get_dump_info(ArchDumpInfo *info,
                      const GuestPhysBlockList *guest_phys_blocks)
{
    info->d_machine = EM_XR17032;
    info->d_endian = ELFDATA2LSB;
    info->d_class = ELFCLASS32;

    return 0;
}

ssize_t cpu_get_note_size(int class, int machine, int nr_cpus)
{
    size_t note_size = 0;

    if (class == ELFCLASS32) {
        note_size = XR17032_PRSTATUS_NOTE_SIZE;
    }

    return note_size * nr_cpus;
}

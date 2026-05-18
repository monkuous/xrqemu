/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 monkuous
 */
#include "qemu/osdep.h"
#include "cpu.h"
#include "cr.h"

#define CR_OFF_FUNCS(NAME, FL, RD, WR)                     \
    [XR17032_CR_##NAME] = {                                \
        .name   = (stringify(NAME)),                       \
        .offset = offsetof(CPUXR17032State, CR_##NAME),    \
        .flags = FL, .readfn = RD, .writefn = WR           \
    }

#define CR_OFF_FLAGS(NAME, FL)   CR_OFF_FUNCS(NAME, FL, NULL, NULL)
#define CR_OFF(NAME)             CR_OFF_FLAGS(NAME, 0)

static CRInfo cr_info[] = {
    CR_OFF(RS),
    CR_OFF(WHAMI),
    CR_OFF(EB),
    CR_OFF(EPC),
    CR_OFF(EBADADDR),
    CR_OFF(TBMISSADDR),
    CR_OFF(TBPC),
    CR_OFF(SCRATCH0),
    CR_OFF(SCRATCH1),
    CR_OFF(SCRATCH2),
    CR_OFF(SCRATCH3),
    CR_OFF(SCRATCH4),
    CR_OFF_FLAGS(ITBPTE, CRFL_TLBFILL),
    CR_OFF_FLAGS(ITBTAG, CRFL_EXITTB),
    CR_OFF(ITBINDEX),
    CR_OFF_FLAGS(ITBCTRL, CRFL_STOP),
    CR_OFF_FLAGS(ICACHECTRL, CRFL_READONLY),
    CR_OFF(ITBADDR),
    CR_OFF_FLAGS(DTBPTE, CRFL_TLBFILL),
    CR_OFF_FLAGS(DTBTAG, CRFL_EXITTB),
    CR_OFF(DTBINDEX),
    CR_OFF_FLAGS(DTBCTRL, CRFL_STOP),
    CR_OFF_FLAGS(DCACHECTRL, CRFL_READONLY),
    CR_OFF(DTBADDR),
};

CRInfo *get_cr(unsigned int cr_num)
{
    CRInfo *cr;

    if (cr_num >= ARRAY_SIZE(cr_info)) {
        return NULL;
    }

    cr = &cr_info[cr_num];
    if (cr->offset == 0) {
        return NULL;
    }

    return cr;
}

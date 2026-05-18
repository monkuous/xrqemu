/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2026 monkuous
 */

#ifndef TARGET_XR17032_CR_H
#define TARGET_XR17032_CR_H

#include "cpu-cr.h"

typedef void (*GenCRFunc)(void);
enum {
    CRFL_READONLY = (1 << 0),
    CRFL_EXITTB   = (1 << 1),
    CRFL_STOP     = (1 << 2),
    CRFL_TLBFILL  = (1 << 3),
};

typedef struct {
    const char *name;
    int offset;
    int flags;
    GenCRFunc readfn;
    GenCRFunc writefn;
} CRInfo;

CRInfo *get_cr(unsigned int cr_num);

#endif /* TARGET_XR17032_CR_H */

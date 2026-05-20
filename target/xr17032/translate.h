/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/17032 translation routines.
 *
 * Copyright (c) 2026 monkuous
 */

#ifndef TARGET_XR17032_TRANSLATE_H
#define TARGET_XR17032_TRANSLATE_H

#include "exec/translator.h"

#define TRANS(NAME, AVAIL, FUNC, ...) \
    static bool trans_##NAME(DisasContext *ctx, arg_##NAME * a) \
    { return avail_##AVAIL(ctx) && FUNC(ctx, a, __VA_ARGS__); }

#define avail_ALL(C)   true

typedef enum {
    SHIFT_LEFT = 0b00,
    SHIFT_RIGHT = 0b01,
    SHIFT_ARITHMETIC = 0b10,
    SHIFT_ROTATE = 0b11,
} DisasShift;

typedef struct DisasContext {
    DisasContextBase base;
    target_ulong page_start;
    uint32_t opcode;
    uint16_t mem_idx;
    uint16_t plv;
    TCGv zero;
} DisasContext;

extern TCGv cpu_gpr[32], cpu_pc;

#endif

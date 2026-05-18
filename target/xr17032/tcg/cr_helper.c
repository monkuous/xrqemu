/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * XR/17032 emulation helpers for CRs
 *
 * Copyright (c) 2026 monkuous
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/main-loop.h"
#include "cpu.h"
#include "internals.h"
#include "qemu/host-utils.h"
#include "exec/helper-proto.h"
#include "exec/cputlb.h"
#include "accel/tcg/cpu-ldst.h"
#include "hw/core/irq.h"
#include "cpu-cr.h"
#include "cpu-mmu.h"

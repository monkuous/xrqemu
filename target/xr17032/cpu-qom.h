/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * QEMU XR/17032 CPU QOM header (target agnostic)
 *
 * Copyright (c) 2026 monkuous
 */

#ifndef XR17032_CPU_QOM_H
#define XR17032_CPU_QOM_H

#include "hw/core/cpu.h"

#define TYPE_XR17032_CPU "xr17032-cpu"

OBJECT_DECLARE_CPU_TYPE(XR17032CPU, XR17032CPUClass,
                        XR17032_CPU)

#define XR17032_CPU_TYPE_SUFFIX "-" TYPE_XR17032_CPU
#define XR17032_CPU_TYPE_NAME(model) model XR17032_CPU_TYPE_SUFFIX

#define TYPE_XR17032_CPU_BASE XR17032_CPU_TYPE_NAME("base")

#endif

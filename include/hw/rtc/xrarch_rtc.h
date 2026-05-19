/*
 * XR/arch Real Time Clock emulation
 *
 * Copyright (C) 2026 monkuous
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

#ifndef HW_RTC_GOLDFISH_RTC_H
#define HW_RTC_GOLDFISH_RTC_H

#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_XRARCH_RTC "xrarch.rtc"
OBJECT_DECLARE_SIMPLE_TYPE(XRarchRtcState, XRARCH_RTC)

struct XRarchRtcState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;

    uint32_t data;
    uint32_t interval;
    int64_t offset_rtc;

    int64_t last_time;
    QEMUTimer *timer;
    qemu_irq irq;
};

#endif

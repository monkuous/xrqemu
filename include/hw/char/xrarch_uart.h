/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * XR/arch UART
 *
 * (c) 2026 monkuous
 *
 */

#ifndef HW_CHAR_XRARCH_UART_H
#define HW_CHAR_XRARCH_UART_H

#include "qemu/fifo8.h"
#include "chardev/char-fe.h"
#include "hw/core/sysbus.h"

#define TYPE_XRARCH_UART "xrarch.uart"
OBJECT_DECLARE_SIMPLE_TYPE(XRarchUartState, XRARCH_UART)

#define XRARCH_UART_BUFFER_SIZE 32

struct XRarchUartState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;
    CharFrontend chr;

    bool int_enabled;
    Fifo8 rx_fifo;
};

#endif

/*
 * QEMU EBus emulator.
 *
 * Copyright (c) 2026 monkuous
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/misc/ebus.h"
#include "qom/object.h"
#include "hw/core/sysbus.h"

static const TypeInfo ebus_device_info = {
    .name = TYPE_EBUS_DEVICE,
    .parent = TYPE_DYNAMIC_SYS_BUS_DEVICE,
    .abstract = true,
    .instance_size = sizeof(SysBusDevice),
};

static void ebus_register_types(void)
{
    type_register_static(&ebus_device_info);
}

type_init(ebus_register_types)

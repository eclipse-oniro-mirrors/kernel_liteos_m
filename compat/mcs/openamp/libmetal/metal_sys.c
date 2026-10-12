/*
 * Copyright (c) 2015, Xilinx Inc. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * LiteOS-M (RK3568 AMP slave core) system backend for libmetal.
 *
 * The companion headers live in libmetal-system/ next to this file and are
 * overlaid onto the generated include set by the board Makefile (system
 * name "liteos"). The slave core needs no bus/probe/irq infrastructure:
 * memory is identity-mapped (MMU off) and IRQs are routed through the
 * LiteOS-M GIC port by the mica platform layer, not by libmetal.
 */

#include <metal/sys.h>
#include <metal/device.h>
#include <metal/io.h>

struct metal_state _metal;

int metal_sys_init(const struct metal_init_params *params)
{
    (void)params;
    /* every libmetal system backend registers the generic bus during init
     * (see system/{generic,freertos,zephyr}/init.c); metal_device_open on
     * the baremetal pedestal depends on it. */
    return metal_bus_register(&metal_generic_bus);
}

void metal_sys_finish(void)
{
}

/*
 * libmetal core (device.c/io.c) references these two system hooks. On this
 * bare-metal slave the I/O regions handed over by the mica pedestal are
 * already identity-mapped (MMU off, virt == phys), so both are no-ops.
 */
void metal_sys_io_mem_map(struct metal_io_region *io)
{
    (void)io;
}

int metal_generic_dev_sys_open(struct metal_device *dev)
{
    (void)dev;
    return 0;
}

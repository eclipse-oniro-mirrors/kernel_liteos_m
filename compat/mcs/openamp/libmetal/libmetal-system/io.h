/*
 * Copyright (c) 2017, Linaro Limited. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/io.h
 * @brief	LiteOS-M specific io definitions.
 */

#ifndef __METAL_IO__H__
#error "Include metal/io.h instead of metal/liteos/io.h"
#endif

#ifndef __METAL_LITEOS_IO__H__
#define __METAL_LITEOS_IO__H__

#ifdef __cplusplus
extern "C" {
#endif

#ifdef METAL_INTERNAL
/**
 * @brief memory mapping for an I/O region
 *
 * LiteOS-M runs the slave core with MMU disabled: shared memory and device
 * regions are physical addresses that are directly dereferenceable, so no
 * mapping is required.
 */
static inline void metal_sys_io_mem_map(struct metal_io_region *io)
{
	(void)io;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* __METAL_LITEOS_IO__H__ */

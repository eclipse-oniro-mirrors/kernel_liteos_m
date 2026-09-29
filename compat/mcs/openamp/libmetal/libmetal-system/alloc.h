/*
 * Copyright (c) 2017, Linaro Limited. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/alloc.h
 * @brief	LiteOS-M libmetal memory allocation definitions.
 */

#ifndef __METAL_ALLOC__H__
#error "Include metal/alloc.h instead of metal/liteos/alloc.h"
#endif

#ifndef __METAL_LITEOS_ALLOC__H__
#define __METAL_LITEOS_ALLOC__H__

#include "los_memory.h"
#include "target_config.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void *metal_allocate_memory(unsigned int size)
{
	return LOS_MemAlloc((VOID *)LOSCFG_SYS_HEAP_ADDR, size);
}

static inline void metal_free_memory(void *ptr)
{
	LOS_MemFree((VOID *)LOSCFG_SYS_HEAP_ADDR, ptr);
}

#ifdef __cplusplus
}
#endif

#endif /* __METAL_LITEOS_ALLOC__H__ */

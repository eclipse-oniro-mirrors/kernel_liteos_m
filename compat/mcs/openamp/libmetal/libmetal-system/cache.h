/*
 * Copyright (c) 2018, Linaro Limited. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/cache.h
 * @brief	LiteOS-M cache operation primitives for libmetal.
 *
 * The slave core keeps its caches enabled while the shared rpmsg memory
 * (0x74000000 window) is Normal Cacheable memory, so every buffer handed
 * to/from the master core must be explicitly maintained. Cortex-A55 cache
 * line size is 64 bytes.
 */

#ifndef __METAL_CACHE__H__
#error "Include metal/cache.h instead of metal/liteos/cache.h"
#endif

#ifndef __METAL_LITEOS_CACHE__H__
#define __METAL_LITEOS_CACHE__H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LITEOS_CACHE_LINE_SZ 64

static inline void __metal_cache_flush(void *addr, unsigned int len)
{
	uintptr_t start = (uintptr_t)addr & ~((uintptr_t)LITEOS_CACHE_LINE_SZ - 1U);
	uintptr_t end = (uintptr_t)addr + len;

	for (; start < end; start += LITEOS_CACHE_LINE_SZ) {
		__asm__ volatile("dc civac, %0" : : "r"(start) : "memory");
	}
	__asm__ volatile("dsb sy" : : : "memory");
}

static inline void __metal_cache_invalidate(void *addr, unsigned int len)
{
	uintptr_t start = (uintptr_t)addr & ~((uintptr_t)LITEOS_CACHE_LINE_SZ - 1U);
	uintptr_t end = (uintptr_t)addr + len;

	for (; start < end; start += LITEOS_CACHE_LINE_SZ) {
		__asm__ volatile("dc ivac, %0" : : "r"(start) : "memory");
	}
	__asm__ volatile("dsb sy" : : : "memory");
}

#ifdef __cplusplus
}
#endif

#endif /* __METAL_LITEOS_CACHE__H__ */

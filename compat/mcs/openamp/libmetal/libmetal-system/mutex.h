/*
 * Copyright (c) 2017, Linaro Limited. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/mutex.h
 * @brief	LiteOS-M mutex primitives for libmetal.
 *
 * The slave core is single-core, so a libmetal mutex maps to an
 * interrupt lock (save/restore of the interrupt state) instead of a
 * LOS_Mux lock: ArchIntLock() masks interrupts and returns the saved
 * DAIF state, ArchIntRestore() puts it back. This also protects
 * against the tick interrupt.
 */

#ifndef __METAL_MUTEX__H__
#error "Include metal/mutex.h instead of metal/liteos/mutex.h"
#endif

#ifndef __METAL_LITEOS_MUTEX__H__
#define __METAL_LITEOS_MUTEX__H__

#include "los_interrupt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef UINT32 metal_mutex_t;

/*
 * METAL_MUTEX_INIT - used for initializing an mutex element in a static struct
 * or global
 */
#define METAL_MUTEX_INIT(m) 0
/*
 * METAL_MUTEX_DEFINE - used for defining and initializing a global or
 * static singleton mutex
 */
#define METAL_MUTEX_DEFINE(m) metal_mutex_t m = 0

static inline void __metal_mutex_init(metal_mutex_t *mutex)
{
	*mutex = 0;
}

static inline void __metal_mutex_deinit(metal_mutex_t *mutex)
{
	*mutex = 0;
}

static inline int __metal_mutex_try_acquire(metal_mutex_t *mutex)
{
	*mutex = ArchIntLock();
	return 0;
}

static inline int __metal_mutex_is_acquired(metal_mutex_t *mutex)
{
	/* interrupt state is saved, not a counter: report "not held" */
	(void)mutex;
	return 0;
}

static inline void __metal_mutex_acquire(metal_mutex_t *mutex)
{
	*mutex = ArchIntLock();
}

static inline void __metal_mutex_release(metal_mutex_t *mutex)
{
	ArchIntRestore(*mutex);
}

#ifdef __cplusplus
}
#endif

#endif /* __METAL_LITEOS_MUTEX__H__ */

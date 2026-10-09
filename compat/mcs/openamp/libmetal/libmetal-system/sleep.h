/*
 * Copyright (c) 2018, Linaro Limited. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/sleep.h
 * @brief	LiteOS-M sleep primitives for libmetal.
 */

#ifndef __METAL_SLEEP__H__
#error "Include metal/sleep.h instead of metal/liteos/sleep.h"
#endif

#ifndef __METAL_LITEOS_SLEEP__H__
#define __METAL_LITEOS_SLEEP__H__

#include "los_task.h"
#include "target_config.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline int __metal_sleep_usec(unsigned int usec)
{
	UINT32 ticks = (UINT32)(LOSCFG_BASE_CORE_TICK_PER_SECOND * usec / 1000000UL);

	/* tick granularity is 10 ms; sub-tick sleeps just yield the CPU */
	if (ticks == 0) {
		ticks = 1;
	}
	return (int)LOS_TaskDelay(ticks);
}

#ifdef __cplusplus
}
#endif

#endif /* __METAL_LITEOS_SLEEP__H__ */

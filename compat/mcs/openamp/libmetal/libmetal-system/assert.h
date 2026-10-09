/*
 * Copyright (c) 2018, Xilinx Inc. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/assert.h
 * @brief	LiteOS-M assertion support.
 */

#ifndef __METAL_ASSERT__H__
#error "Include metal/assert.h instead of metal/liteos/assert.h"
#endif

#ifndef __METAL_LITEOS_ASSERT__H__
#define __METAL_LITEOS_ASSERT__H__

#include "los_debug.h"

/**
 * @brief Assertion macro.
 * @param cond Condition to test.
 */
#define metal_sys_assert(cond)						\
	do {								\
		if (!(cond)) {						\
			PRINT_ERR("libmetal assert fail: %s (%s:%d)\n",	\
				  #cond, __func__, __LINE__);		\
			while (1) {					\
			}						\
		}							\
	} while (0)

#endif /* __METAL_LITEOS_ASSERT__H */

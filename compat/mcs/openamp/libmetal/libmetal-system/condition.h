/*
 * Copyright (c) 2017, Linaro Limited. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/condition.h
 * @brief	LiteOS-M condition variable primitives for libmetal.
 */

#ifndef __METAL_CONDITION__H__
#error "Include metal/condition.h instead of metal/liteos/condition.h"
#endif

#ifndef __METAL_LITEOS_CONDITION__H__
#define __METAL_LITEOS_CONDITION__H__

#include <metal/mutex.h>
#include <metal/utilities.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Condition variables are not used by the baremetal rpmsg path; the
 * generic no-op definitions in metal/condition.h apply.
 */

#ifdef __cplusplus
}
#endif

#endif /* __METAL_LITEOS_CONDITION__H__ */

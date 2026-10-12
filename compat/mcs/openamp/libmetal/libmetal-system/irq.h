/*
 * Copyright (c) 2017, Linaro Limited. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/irq.h
 * @brief	LiteOS-M libmetal irq definitions.
 */

#ifndef __METAL_IRQ__H__
#error "Include metal/irq.h instead of metal/liteos/irq.h"
#endif

#ifndef __METAL_LITEOS_IRQ__H__
#define __METAL_LITEOS_IRQ__H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The rpmsg backend registers its interrupts directly through the LiteOS-M
 * HAL (LOS_HwiCreate, GICv3 INTID semantics), so no libmetal irq-controller
 * plumbing is needed here.
 */

#ifdef __cplusplus
}
#endif

#endif /* __METAL_LITEOS_IRQ__H__ */

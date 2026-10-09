/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * SPDX-License-Identifier: MulanPSL-2.0
 */

/*
 * @file    liteos/io.h
 * @brief   LiteOS-M io primitives for the libmica platform layer.
 *
 * The slave core runs with the MMU disabled: device and shared-memory
 * regions are physical addresses that are directly dereferenceable.
 */

#ifndef __MICA_IO__H__
#error "Include mica/platform/io.h instead of mica/platform/system/liteos/io.h"
#endif

#ifndef __MICA_LITEOS_IO__H__
#define __MICA_LITEOS_IO__H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void mica_writeb(uint8_t val, unsigned long addr)
{
    *(volatile uint8_t *)addr = val;
}

static inline void mica_writew(uint16_t val, unsigned long addr)
{
    *(volatile uint16_t *)addr = val;
}

static inline void mica_writel(uint32_t val, unsigned long addr)
{
    *(volatile uint32_t *)addr = val;
}

#ifdef __cplusplus
}
#endif

#endif /* __MICA_LITEOS_IO__H__ */

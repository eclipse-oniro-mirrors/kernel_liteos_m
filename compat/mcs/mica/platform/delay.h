/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * SPDX-License-Identifier: MulanPSL-2.0
 */

/*
 * @file    liteos/delay.h
 * @brief   LiteOS-M delay backend for the libmica platform layer.
 */

#ifndef __MICA_DELAY__H__
#error "Include mica/platform/delay.h instead of mica/platform/system/liteos/delay.h"
#endif

#ifndef __MICA_LITEOS_DELAY__H__
#define __MICA_LITEOS_DELAY__H__

#include <stdint.h>
#include "los_task.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline void mica_delay_tick(uint32_t tick)
{
    (VOID)LOS_TaskDelay((UINT32)tick);
}

#ifdef __cplusplus
}
#endif

#endif /* __MICA_LITEOS_DELAY__H__ */

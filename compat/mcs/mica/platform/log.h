/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * SPDX-License-Identifier: MulanPSL-2.0
 */

/*
 * @file    liteos/log.h
 * @brief   LiteOS-M log backend for the libmica platform layer.
 *
 * Log routing: once the rpmsg tty service is up, mica logs travel over
 * rpmsg and are collected on the master core. Before that (early
 * bring-up) or on send failure, fall back to the slave-core debug UART
 * through the LiteOS-M PRINTK.
 */

#ifndef __MICA_LOG__H__
#error "Include mica/platform/log.h instead of mica/platform/system/liteos/log.h"
#endif

#ifndef __MICA_LITEOS_LOG__H__
#define __MICA_LITEOS_LOG__H__

#include "los_debug.h"
#include <mica/service.h>

/*
 * mica_tty_printf: >0 = bytes sent over rpmsg; 0 = tty service not ready
 * yet (early bring-up); <0 = send failure. Only the non-positive cases
 * fall back to the UART console.
 */
#define mica_log(fmt, ...)                                  \
    do {                                                    \
        if (mica_tty_printf(fmt, ##__VA_ARGS__) <= 0) {     \
            PRINTK(fmt, ##__VA_ARGS__);                     \
        }                                                   \
    } while (0)

#endif /* __MICA_LITEOS_LOG__H__ */

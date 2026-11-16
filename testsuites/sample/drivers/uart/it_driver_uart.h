/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* it_driver_uart.h — uart 驱动域自持头（2026-11-14 自 It_mcs_drv.h 拆分，域间解耦） */
#ifndef _IT_DRIVER_UART_H_
#define _IT_DRIVER_UART_H_

#include "osTest.h"
#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"

#ifdef LOSCFG_RK3568_UART
/* UART 驱动域（board 头 uart_drv.h 由用例文件自行 include） */

/* 域聚合 */
VOID ItDriverUartSuite(VOID);
/* 用例（api 001-006 / 功能 201-204 / 压力 301-302） */
VOID ItDriverUart001(VOID);
VOID ItDriverUart002(VOID);
VOID ItDriverUart003(VOID);
VOID ItDriverUart004(VOID);
VOID ItDriverUart005(VOID);
VOID ItDriverUart006(VOID);
VOID ItDriverUart201(VOID);
VOID ItDriverUart202(VOID);
VOID ItDriverUart203(VOID);
VOID ItDriverUart204(VOID);
VOID ItDriverUart301(VOID);
VOID ItDriverUart302(VOID);
#endif /* LOSCFG_RK3568_UART */
#endif

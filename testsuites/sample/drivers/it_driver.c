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

/* it_driver.c — 让渡驱动域总聚合（ItDriverSuite；osTest 挂 TestKernel 前调用） */
#include "can/it_driver_can.h"
#include "ge/it_driver_net.h"
#include "uart/it_driver_uart.h"
#include "timer/it_driver_timer.h"

VOID ItDriverSuite(VOID)
{
#ifdef LOSCFG_MCS_CAN
    ItDriverCanSuite();
#endif
#ifdef LOSCFG_MCS_NET
    ItDriverNetSuite();
#endif
#ifdef LOSCFG_RK3568_UART
    ItDriverUartSuite();
#endif
#ifdef LOSCFG_MCS_TIMER
    ItDriverTimerSuite();
#endif
}

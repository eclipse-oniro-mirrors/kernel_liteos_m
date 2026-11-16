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

/* it_driver_timer.h — timer 驱动域自持头（2026-11-14 自 It_mcs_drv.h 拆分，域间解耦） */
#ifndef _IT_DRIVER_TIMER_H_
#define _IT_DRIVER_TIMER_H_

#include "osTest.h"
#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_task.h"

#ifdef LOSCFG_MCS_TIMER
#include "timer.h"

#define IT_TIMER_US_PER_TICK \
    (1000000ULL / LOSCFG_BASE_CORE_TICK_PER_SECOND)

/* irq-mode expiry recorder: the callback runs in ISR context */
typedef struct {
    timerDevice *dev;
    volatile uint32_t count;
} ItDriverTimerCtx;

static inline void ItDriverTimerCb(void *arg)
{
    ItDriverTimerCtx *c = (ItDriverTimerCtx *)arg;

    c->count++;
}

/* wait until ctx->count reaches want or timeoutMs elapses; 0 on hit */
static inline int ItDriverTimerWaitCount(ItDriverTimerCtx *c, uint32_t want,
                                         uint32_t timeoutMs)
{
    uint32_t waited = 0;

    while (c->count < want) {
        if (waited >= timeoutMs) {
            return -1;
        }
        LOS_TaskDelay(1);
        waited += (uint32_t)(IT_TIMER_US_PER_TICK / 1000ULL) + 1;
    }
    return 0;
}

/* 域聚合 */
VOID ItDriverTimerSuite(VOID);
/* 用例（功能 201-211） */
VOID ItDriverTimer201(VOID);
VOID ItDriverTimer202(VOID);
VOID ItDriverTimer203(VOID);
VOID ItDriverTimer204(VOID);
VOID ItDriverTimer205(VOID);
VOID ItDriverTimer206(VOID);
VOID ItDriverTimer207(VOID);
VOID ItDriverTimer208(VOID);
VOID ItDriverTimer209(VOID);
VOID ItDriverTimer210(VOID);
VOID ItDriverTimer211(VOID);
#endif /* LOSCFG_MCS_TIMER */
#endif

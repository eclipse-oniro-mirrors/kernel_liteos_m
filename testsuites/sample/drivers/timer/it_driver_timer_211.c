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

/* ItDriverTimer211: 500ms medium-period timing: four expiries within ±2 ticks across the 24MHz conversion */
#include "it_driver_timer.h"

static UINT32 Testcase(VOID)
{
    timerConfig cfg;
    timerDevice *dev = NULL;
    ItDriverTimerCtx ctx;
    UINT64 startTick, endTick, expectTicks, diff;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.mode = TIMER_MODE_PERIODIC;
    cfg.intervalUs = 500000; /* 500ms */
    cfg.callback = ItDriverTimerCb;
    cfg.callbackArg = &ctx;
    (void)memset(&ctx, 0, sizeof(ctx));

    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ctx.dev = dev;

    startTick = (UINT64)LOS_TickCountGet();
    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = ItDriverTimerWaitCount(&ctx, 4, 8000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    endTick = (UINT64)LOS_TickCountGet();

    LOS_TaskDelay(1);
    (void)TimerStop(dev);

    /* 4 x 500ms = 2000ms: LOAD = 12,000,000 - 1 counts at 24MHz - the
     * largest period in the suite, exercising the us->ticks conversion
     * at scale, measured across the tick counter */
    {
        UINT64 span = endTick - startTick;

        expectTicks = 2000000ULL / IT_TIMER_US_PER_TICK;
        diff = (span > expectTicks) ? span - expectTicks : expectTicks - span;
    }
    ICUNIT_GOTO_EQUAL((diff <= 2), 1, (UINT32)diff, EXIT);

    ret = TimerDeinit(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)TimerStop(dev);
        (void)TimerDeinit(dev);
    }
    return LOS_NOK;
}

VOID ItDriverTimer211(VOID)
{
    TEST_ADD_CASE("ItDriverTimer211", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_TIMER, TEST_LEVEL1, TEST_FUNCTION);
}

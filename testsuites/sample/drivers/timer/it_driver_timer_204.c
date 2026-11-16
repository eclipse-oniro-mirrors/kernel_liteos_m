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

/* ItDriverTimer204: TimerSetInterval on a running channel: after shrinking
 * 20ms -> 5ms the next 10 expiries land within two LOS ticks of the
 * 50ms total (tick-level check; the driver exposes no elapsed readout
 * in the minimal contract) */
#include "it_driver_timer.h"

static UINT32 Testcase(VOID)
{
    timerConfig cfg;
    timerDevice *dev = NULL;
    ItDriverTimerCtx ctx;
    UINT64 startTick, endTick, expectTicks, diff;
    uint32_t base;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.mode = TIMER_MODE_PERIODIC;
    cfg.intervalUs = 20000; /* 20ms */
    cfg.callback = ItDriverTimerCb;
    cfg.callbackArg = &ctx;
    (void)memset(&ctx, 0, sizeof(ctx));

    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ctx.dev = dev;

    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = ItDriverTimerWaitCount(&ctx, 1, 2000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = TimerSetInterval(dev, 5000); /* 20ms -> 5ms */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    base = ctx.count;
    startTick = (UINT64)LOS_TickCountGet();
    ret = ItDriverTimerWaitCount(&ctx, base + 10, 2000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    endTick = (UINT64)LOS_TickCountGet();

    LOS_TaskDelay(1);
    (void)TimerStop(dev);

    /* 10 x 5ms = 50ms, measured across the tick counter */
    {
        UINT64 span = endTick - startTick;

        expectTicks = 50000ULL / IT_TIMER_US_PER_TICK;
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

VOID ItDriverTimer204(VOID)
{
    TEST_ADD_CASE("ItDriverTimer204", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_TIMER, TEST_LEVEL0, TEST_FUNCTION);
}

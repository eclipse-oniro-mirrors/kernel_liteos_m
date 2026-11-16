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

/* ItDriverTimer207: ISR-context TimerSetInterval: callback shrinks its own period 10ms→5ms, new rate takes effect without deadlock */
#include "it_driver_timer.h"

/* ISR return-code relay: assertions cannot run in interrupt context,
 * so the in-callback TimerSetInterval result lands here and the main
 * task asserts it afterwards (-1 = the shrink never ran). */
static volatile int g_isrSetRet = -1;

static void ItDriverTimer207Cb(void *arg)
{
    ItDriverTimerCtx *c = (ItDriverTimerCtx *)arg;

    c->count++;
    if (c->count == 3) { /* third expiry: shrink 10ms -> 5ms in the ISR */
        g_isrSetRet = TimerSetInterval(c->dev, 5000);
    }
}

static UINT32 Testcase(VOID)
{
    timerConfig cfg;
    timerDevice *dev = NULL;
    ItDriverTimerCtx ctx;
    UINT64 startTick, endTick, expectTicks, diff;
    int ret;

    g_isrSetRet = -1;
    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.mode = TIMER_MODE_PERIODIC;
    cfg.intervalUs = 10000; /* 10ms */
    cfg.callback = ItDriverTimer207Cb;
    cfg.callbackArg = &ctx;
    (void)memset(&ctx, 0, sizeof(ctx));

    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ctx.dev = dev;

    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* two expiries at 10ms, then the third callback shrinks the period
     * to 5ms from inside the ISR (the contract's one ISR-safe call) */
    ret = ItDriverTimerWaitCount(&ctx, 2, 3000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = ItDriverTimerWaitCount(&ctx, 3, 3000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* six expiries at the new 5ms rate must land near 30ms total */
    startTick = (UINT64)LOS_TickCountGet();
    ret = ItDriverTimerWaitCount(&ctx, 9, 3000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    endTick = (UINT64)LOS_TickCountGet();

    /* 6 x 5ms = 30ms, measured across the tick counter */
    {
        UINT64 span = endTick - startTick;

        expectTicks = 30000ULL / IT_TIMER_US_PER_TICK;
        diff = (span > expectTicks) ? span - expectTicks : expectTicks - span;
    }
    ICUNIT_GOTO_EQUAL((diff <= 2), 1, (UINT32)diff, EXIT);

    /* the in-ISR shrink must have succeeded: a deadlock would hang the
     * bounded waits above, a failed call surfaces here */
    ICUNIT_GOTO_EQUAL(g_isrSetRet, 0, g_isrSetRet, EXIT);

    LOS_TaskDelay(1);
    (void)TimerStop(dev);

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

VOID ItDriverTimer207(VOID)
{
    TEST_ADD_CASE("ItDriverTimer207", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_TIMER, TEST_LEVEL0, TEST_FUNCTION);
}

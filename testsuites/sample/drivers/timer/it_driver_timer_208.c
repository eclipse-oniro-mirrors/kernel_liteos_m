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

/* ItDriverTimer208: oneshot expiry state machine: after expiry Stop is idempotent and a fresh Start rearms exactly once more */
#include "it_driver_timer.h"

static UINT32 Testcase(VOID)
{
    timerConfig cfg;
    timerDevice *dev = NULL;
    ItDriverTimerCtx ctx;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.mode = TIMER_MODE_ONESHOT;
    cfg.intervalUs = 50000; /* 50ms */
    cfg.callback = ItDriverTimerCb;
    cfg.callbackArg = &ctx;
    (void)memset(&ctx, 0, sizeof(ctx));

    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ctx.dev = dev;

    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* exactly one expiry */
    ret = ItDriverTimerWaitCount(&ctx, 1, 2000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 4x the period of silence: the spent channel must stay silent */
    LOS_TaskDelay((UINT32)(200000ULL / IT_TIMER_US_PER_TICK));
    ICUNIT_GOTO_EQUAL(ctx.count, 1, ctx.count, EXIT);

    /* stop after the self-stopped expiry: the channel parked itself at
     * zero but the driver state machine did not - stop must still
     * succeed and be idempotent */
    ret = TimerStop(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* a fresh start rearms the spent oneshot exactly once more */
    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = ItDriverTimerWaitCount(&ctx, 2, 2000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* and it goes silent again after the second expiry */
    LOS_TaskDelay((UINT32)(200000ULL / IT_TIMER_US_PER_TICK));
    ICUNIT_GOTO_EQUAL(ctx.count, 2, ctx.count, EXIT);

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

VOID ItDriverTimer208(VOID)
{
    TEST_ADD_CASE("ItDriverTimer208", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_TIMER, TEST_LEVEL0, TEST_FUNCTION);
}

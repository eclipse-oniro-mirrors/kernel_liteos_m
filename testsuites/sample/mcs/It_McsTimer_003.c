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

/* ItMcsTimer003: oneshot-irq: exactly one callback for a 50ms one-shot
 * and the channel stays silent afterwards */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    timerConfig cfg;
    timerDevice *dev = NULL;
    ItMcsTimerCtx ctx;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.mode = TIMER_MODE_ONESHOT;
    cfg.intervalUs = 50000; /* 50ms */
    cfg.callback = ItMcsTimerCb;
    cfg.callbackArg = &ctx;
    (void)memset(&ctx, 0, sizeof(ctx));

    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ctx.dev = dev;

    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = ItMcsTimerWaitCount(&ctx, 1, 2000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 4x the period of silence: a runaway periodic would land here */
    LOS_TaskDelay((UINT32)(200000ULL / IT_TIMER_US_PER_TICK));
    ICUNIT_GOTO_EQUAL(ctx.count, 1, ctx.count, EXIT);

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

VOID ItMcsTimer003(VOID)
{
    TEST_ADD_CASE("ItMcsTimer003", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

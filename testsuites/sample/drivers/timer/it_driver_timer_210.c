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

/* ItDriverTimer210: conversion floor and post-deinit handle invalidation: SetInterval(1us) accepted (24MHz floor), all calls on a deinit'd handle fail */
#include "it_driver_timer.h"

static UINT32 Testcase(VOID)
{
    timerConfig cfg;
    timerDevice *dev = NULL;
    ItDriverTimerCtx ctx;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.mode = TIMER_MODE_PERIODIC;
    cfg.intervalUs = 10000; /* 10ms */
    cfg.callback = ItDriverTimerCb;
    cfg.callbackArg = &ctx;
    (void)memset(&ctx, 0, sizeof(ctx));

    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ctx.dev = dev;

    /* conversion floor: the contract lower bound is 1us ("1us..~179s
     * at 24MHz") - at 24MHz 1us converts to 24 counter ticks, far
     * above the 2-tick minimum, so the driver accepts it. probed in
     * the READY state (before start): a live 1us periodic would storm
     * the irq line, and this side of TimerSetInterval (READY, program
     * LOAD only) has no other coverage in the suite. */
    ret = TimerSetInterval(dev, 1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* restore the working period before the channel goes live */
    ret = TimerSetInterval(dev, 10000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = ItDriverTimerWaitCount(&ctx, 2, 2000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    LOS_TaskDelay(1);
    (void)TimerStop(dev);

    ret = TimerDeinit(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* after deinit the handle is dead: every call fails on the NULL
     * port check (TIMER_EINVAL) before any state check, matching the
     * deinit-then-start behavior of ItDriverTimer205 */
    ret = TimerSetInterval(dev, 10000);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);
    ret = TimerStop(dev);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);
    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)TimerStop(dev);
        (void)TimerDeinit(dev);
    }
    return LOS_NOK;
}

VOID ItDriverTimer210(VOID)
{
    TEST_ADD_CASE("ItDriverTimer210", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_TIMER, TEST_LEVEL0, TEST_FUNCTION);
}

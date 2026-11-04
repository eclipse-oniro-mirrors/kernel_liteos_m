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

/* ItMcsTimer005: argument / state error paths: NULL pointers, bad
 * portId, zero interval, re-init while running, deinit-then-start */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    timerConfig cfg;
    timerDevice *dev = NULL;
    timerDevice *dev2 = NULL;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.mode = TIMER_MODE_PERIODIC;
    cfg.intervalUs = 10000;

    ret = TimerInit(NULL, &dev);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);

    ret = TimerInit(&cfg, NULL);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);

    cfg.portId = 0;
    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, TIMER_ENODEV, ret, EXIT);

    cfg.portId = 99;
    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, TIMER_ENODEV, ret, EXIT);

    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.intervalUs = 0;
    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);

    cfg.intervalUs = 10000;
    cfg.mode = 2; /* outside the TIMER_MODE_* enum */
    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);
    cfg.mode = TIMER_MODE_PERIODIC;

    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* re-init of the same running port must be refused */
    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = TimerInit(&cfg, &dev2);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EBUSY, ret, EXIT);

    ret = TimerSetInterval(dev, 0);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);

    (void)TimerStop(dev);
    ret = TimerSetInterval(NULL, 1000);
    ICUNIT_GOTO_EQUAL(ret, TIMER_EINVAL, ret, EXIT);

    /* after deinit the handle is dead: TimerStart rejects it on the
     * NULL port check (EINVAL) before any state check */
    ret = TimerDeinit(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
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

VOID ItMcsTimer005(VOID)
{
    TEST_ADD_CASE("ItMcsTimer005", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

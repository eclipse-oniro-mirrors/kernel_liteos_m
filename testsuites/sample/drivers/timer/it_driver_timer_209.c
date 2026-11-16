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

/* ItDriverTimer209: callback=NULL periodic run: pure-irq path with no user callback stays healthy */
#include "it_driver_timer.h"

static UINT32 Testcase(VOID)
{
    timerConfig cfg;
    timerDevice *dev = NULL;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = TIMER_PORT_TIMER1_RK3568;
    cfg.mode = TIMER_MODE_PERIODIC;
    cfg.intervalUs = 20000; /* 20ms */
    cfg.callback = NULL;
    cfg.callbackArg = NULL;

    ret = TimerInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = TimerStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* ~10 periods of pure-irq expiry with no user callback: the run
     * must stay healthy - with no callback there is no count source,
     * so the assertions are the all-zero return codes plus survival */
    LOS_TaskDelay((UINT32)(200000ULL / IT_TIMER_US_PER_TICK));

    ret = TimerStop(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

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

VOID ItDriverTimer209(VOID)
{
    TEST_ADD_CASE("ItDriverTimer209", Testcase, TEST_LOS, TEST_FN_RK3568SDRV_TIMER, TEST_LEVEL1, TEST_FUNCTION);
}

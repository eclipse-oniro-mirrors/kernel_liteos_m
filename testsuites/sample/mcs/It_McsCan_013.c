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
/* ItMcsCan013: acceptance filter (code/mask): only matching ids delivered */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame tx, rx;
    canStats st;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    cfg.filterEn = 1;
    cfg.filterCode = 0x200;
    cfg.filterMask = 0x700; /* exact-id match when mask covers id bits */
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ItMcsCanFillFrame(&tx, 0x100, 0, 8); /* not matching */
    ret = CanSend(dev, &tx, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ItMcsCanFillFrame(&tx, 0x300, 0, 8); /* not matching */
    ret = CanSend(dev, &tx, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ItMcsCanFillFrame(&tx, 0x200, 0, 8); /* matching */
    ret = CanSend(dev, &tx, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    LOS_TaskDelay(2);

    ret = ItMcsCanRecvWait(dev, &rx, 100);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rx.id, 0x200, rx.id, EXIT);

    ret = CanRecv(dev, &rx);
    ICUNIT_GOTO_EQUAL(ret, CAN_EAGAIN, ret, EXIT);

    ret = CanGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(st.rxDelivered, 1, st.rxDelivered, EXIT);

    ret = CanStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsCan013(VOID)
{
    TEST_ADD_CASE("ItMcsCan013", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

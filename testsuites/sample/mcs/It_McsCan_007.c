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
/* ItMcsCan007: CAN FD: FDF+BRS frame (data phase bitrate) round-trip */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame tx, rx;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    cfg.fdEnable = 1;
    cfg.dataBitrate = 2000000;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ItMcsCanFillFrame(&tx, 0x302, CAN_FRAME_FDF | CAN_FRAME_BRS, 48);
    ret = CanSend(dev, &tx, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = ItMcsCanRecvWait(dev, &rx, 100);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(rx.flags, (uint8_t)(CAN_FRAME_FDF | CAN_FRAME_BRS), rx.flags, EXIT);
    ICUNIT_GOTO_EQUAL(rx.len, 48, rx.len, EXIT);
    ICUNIT_GOTO_EQUAL(memcmp(rx.data, tx.data, 48), 0, 1, EXIT);

    ret = CanStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsCan007(VOID)
{
    TEST_ADD_CASE("ItMcsCan007", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

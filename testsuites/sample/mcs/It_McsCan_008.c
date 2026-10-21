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
/* ItMcsCan008: CAN FD legal DLC length sweep (0..8/12/16/20/24/32/48/64) */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    static const uint8_t lens[] = {0, 1, 8, 12, 16, 20, 24, 32, 48, 64};
    canConfig cfg;
    canDevice *dev = NULL;
    canFrame tx, rx;
    uint32_t i;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.mode = CAN_MODE_POLL;
    cfg.workMode = CAN_WORK_LOOPBACK;
    cfg.bitrate = 500000;
    cfg.fdEnable = 1;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = CanStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    for (i = 0; i < sizeof(lens) / sizeof(lens[0]); i++) {
        ItMcsCanFillFrame(&tx, 0x310, CAN_FRAME_FDF, lens[i]);
        ret = CanSend(dev, &tx, 100000);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ret = ItMcsCanRecvWait(dev, &rx, 100);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
        ICUNIT_GOTO_EQUAL(rx.len, lens[i], rx.len, EXIT);
    }

    ret = CanStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsCan008(VOID)
{
    TEST_ADD_CASE("ItMcsCan008", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

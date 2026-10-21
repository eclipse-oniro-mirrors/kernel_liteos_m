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
/* ItMcsNet007: NET_MODE_POLL: send/recv without any IRQ */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    uint8_t frame[64];
    uint32_t len;
    netStats st;
    int ret;

    ItMcsNetCfgAutoneg(&cfg);
    cfg.mode = NET_MODE_POLL;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ItMcsNetFillFrame(frame, sizeof(frame));
    ret = NetSend(dev, frame, sizeof(frame));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = NetRecv(dev, frame, sizeof(frame), &len);
    ICUNIT_GOTO_EQUAL(ret, NET_EAGAIN, ret, EXIT);

    ret = NetGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(st.txAccepted, 1, st.txAccepted, EXIT);
    ICUNIT_GOTO_EQUAL(st.irqCount, 0, st.irqCount, EXIT);

    ret = NetStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsNet007(VOID)
{
    TEST_ADD_CASE("ItMcsNet007", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

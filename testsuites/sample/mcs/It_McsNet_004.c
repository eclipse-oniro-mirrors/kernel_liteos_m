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
/* ItMcsNet004: NetSend parameter validation: NULL dev/frame, len 5/13/1600 */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    uint8_t frame[NET_FRAME_SIZE];
    int ret;

    ItMcsNetCfgAutoneg(&cfg);
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ItMcsNetFillFrame(frame, 64);
    ret = NetSend(NULL, frame, 64);
    ICUNIT_GOTO_EQUAL(ret, NET_EINVAL, ret, EXIT);
    ret = NetSend(dev, NULL, 64);
    ICUNIT_GOTO_EQUAL(ret, NET_EINVAL, ret, EXIT);

    /* complete frame is 14..1514 bytes: both too-short and too-long are
     * length errors (-EMSGSIZE) */
    ret = NetSend(dev, frame, 5);
    ICUNIT_GOTO_EQUAL(ret, NET_EMSGSIZE, ret, EXIT);
    ret = NetSend(dev, frame, 13);
    ICUNIT_GOTO_EQUAL(ret, NET_EMSGSIZE, ret, EXIT);
    ret = NetSend(dev, frame, 1600);
    ICUNIT_GOTO_EQUAL(ret, NET_EMSGSIZE, ret, EXIT);

    /* boundary lengths are legal */
    ItMcsNetFillFrame(frame, 14);
    ret = NetSend(dev, frame, 14);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ItMcsNetFillFrame(frame, 1514);
    ret = NetSend(dev, frame, 1514);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = NetStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsNet004(VOID)
{
    TEST_ADD_CASE("ItMcsNet004", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

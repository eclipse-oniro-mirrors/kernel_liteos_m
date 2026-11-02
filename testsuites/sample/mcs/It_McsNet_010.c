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
/* ItMcsNet010: rx path with live traffic (PRECONDITION: the peer host on the
 * GMAC1 segment generates broadcast traffic, e.g. pings any address of the
 * 192.168.101.0/24 subnet; ambient SSDP/ARP traffic usually suffices) */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    netLinkInfo li;
    uint8_t frame[NET_FRAME_SIZE];
    uint32_t len = 0;
    netStats st;
    int linkUp = 0;
    int got = 0;
    int poll;
    int ret;

    ItMcsNetCfgAutoneg(&cfg);
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* NetInit(autoneg) restarts autonegotiation: the link bounces for ~3s
     * before it is usable again. Wait for it first (tick = 1ms). */
    for (poll = 0; poll < 500; poll++) { /* up to ~5s */
        if ((NetGetLinkInfo(dev, &li) == 0) && li.linkUp) {
            linkUp = 1;
            break;
        }
        LOS_TaskDelay(10);
    }
    ICUNIT_GOTO_EQUAL(linkUp, 1, linkUp, EXIT);

    for (poll = 0; poll < 150; poll++) { /* up to ~15s */
        ret = NetRecv(dev, frame, sizeof(frame), &len);
        if (ret == 0) {
            got = 1;
            break;
        }
        LOS_TaskDelay(100); /* 100ms per iteration (tick = 1ms) */
    }
    ICUNIT_GOTO_EQUAL(got, 1, got, EXIT);
    ICUNIT_GOTO_EQUAL((len >= 60) && (len <= 1514), 1, len, EXIT);

    ret = NetGetStats(dev, &st);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_NOT_EQUAL(st.rxDelivered, 0, st.rxDelivered, EXIT);

    ret = NetStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsNet010(VOID)
{
    TEST_ADD_CASE("ItMcsNet010", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

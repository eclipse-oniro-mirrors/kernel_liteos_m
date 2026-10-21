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
/* ItMcsNet002: NetInit/NetSend/NetRecv parameter and state validation */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    uint8_t frame[64];
    uint32_t len;
    int ret;

    ItMcsNetCfgAutoneg(&cfg);
    ret = NetInit(NULL, &dev);
    ICUNIT_GOTO_EQUAL(ret, NET_EINVAL, ret, EXIT);
    ret = NetInit(&cfg, NULL);
    ICUNIT_GOTO_EQUAL(ret, NET_EINVAL, ret, EXIT);

    cfg.portId = 99; /* unknown port */
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, NET_ENODEV, ret, EXIT);

    ItMcsNetCfgAutoneg(&cfg);
    cfg.mode = (NET_MODE_HYBRID + 1); /* invalid mode */
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, NET_EINVAL, ret, EXIT);

    ItMcsNetCfgAutoneg(&cfg);
    cfg.autoneg = 0;
    cfg.speedMbps = 1000; /* 1000BASE-T cannot be forced */
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, NET_EINVAL, ret, EXIT);

    ItMcsNetCfgAutoneg(&cfg);
    cfg.autoneg = 0;
    cfg.speedMbps = 0; /* force mode without a legal speed */
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, NET_EINVAL, ret, EXIT);

    ItMcsNetCfgAutoneg(&cfg);
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* READY (init done, not started): data-path calls must fail -EIO */
    ItMcsNetFillFrame(frame, sizeof(frame));
    ret = NetSend(dev, frame, sizeof(frame));
    ICUNIT_GOTO_EQUAL(ret, NET_EIO, ret, EXIT);
    ret = NetRecv(dev, frame, sizeof(frame), &len);
    ICUNIT_GOTO_EQUAL(ret, NET_EIO, ret, EXIT);

    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* RUNNING: re-init must fail -EBUSY */
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, NET_EBUSY, ret, EXIT);

    ret = NetStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsNet002(VOID)
{
    TEST_ADD_CASE("ItMcsNet002", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

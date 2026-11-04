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
/* ItMcsNet006: NetGetLinkInfo: snapshot valid, NULL info rejected */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    netLinkInfo li;
    int ret;

    ItMcsNetCfgAutoneg(&cfg);
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = NetGetLinkInfo(dev, NULL);
    ICUNIT_GOTO_EQUAL(ret, NET_EINVAL, ret, EXIT);

    (void)memset(&li, 0, sizeof(li));
    ret = NetGetLinkInfo(dev, &li);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* valid==1: a real PHY snapshot (linkUp==0 is still a valid state);
     * speed/duplex depend on the bench and are not asserted here */
    ICUNIT_GOTO_EQUAL(li.valid, 1, li.valid, EXIT);

    ret = NetStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)NetStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsNet006(VOID)
{
    TEST_ADD_CASE("ItMcsNet006", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

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
/* ItMcsNet008: promisc=0 (station MAC filtered) configuration accepted */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    netConfig cfg;
    netDevice *dev = NULL;
    int ret;

    ItMcsNetCfgAutoneg(&cfg);
    cfg.promisc = 0;
    ret = NetInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = NetStart(dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    netStats st;
    ret = NetGetStats(dev, &st);
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

VOID ItMcsNet008(VOID)
{
    TEST_ADD_CASE("ItMcsNet008", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

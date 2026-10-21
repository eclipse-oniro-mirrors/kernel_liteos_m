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
/* ItMcsCan002: CanInit parameter validation: NULL cfg/dev, bad portId, busy port */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    canConfig cfg;
    canDevice *dev = NULL;
    canDevice *dev2 = NULL;
    int ret;

    (void)memset(&cfg, 0, sizeof(cfg));
    cfg.portId = CAN_PORT_CAN1_RK3568;
    cfg.workMode = CAN_WORK_LOOPBACK;

    ret = CanInit(NULL, &dev);
    ICUNIT_GOTO_EQUAL(ret, CAN_EINVAL, ret, EXIT);
    ret = CanInit(&cfg, NULL);
    ICUNIT_GOTO_EQUAL(ret, CAN_EINVAL, ret, EXIT);

    cfg.portId = 99; /* unknown port */
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, CAN_ENODEV, ret, EXIT);

    cfg.portId = CAN_PORT_CAN1_RK3568;
    ret = CanInit(&cfg, &dev);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* second init while the port is held must fail with -EBUSY */
    ret = CanInit(&cfg, &dev2);
    ICUNIT_GOTO_EQUAL(ret, CAN_EBUSY, ret, EXIT);

    ret = CanStop(dev, 100000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (dev != NULL) {
        (void)CanStop(dev, 100000);
    }
    return LOS_NOK;
}

VOID ItMcsCan002(VOID)
{
    TEST_ADD_CASE("ItMcsCan002", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

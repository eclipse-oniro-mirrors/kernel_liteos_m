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
/* ItMcsSvc006: mica_rcv_data / umt callback parameter validation */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t buf[16];
    size_t len = sizeof(buf);
    int ret;

    /* NOTE: the legal mica_rcv_data() path blocks until a peer frame
     * arrives - only the parameter-validation paths are exercised. */
    ret = mica_rcv_data(NULL, &len);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);
    ret = mica_rcv_data(buf, NULL);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);
    ret = mica_umt_register_rcv_cb(NULL, NULL);
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSvc006(VOID)
{
    TEST_ADD_CASE("ItMcsSvc006", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

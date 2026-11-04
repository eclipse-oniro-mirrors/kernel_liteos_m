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
/* ItMcsRpc001: all three mica services ready, rpc endpoint bound
 * PRECONDITION: the master netproxy worker (netproxy_demo) is running. */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    int ret;

    ret = mica_service_is_ready(MICA_SERVICE_RPC);
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);
    ret = mica_service_is_ready(MICA_SERVICE_TTY);
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);
    ret = mica_service_is_ready(MICA_SERVICE_UMT);
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);
    ret = mica_rpc_is_ready_pub();
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc001(VOID)
{
    TEST_ADD_CASE("ItMcsRpc001", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

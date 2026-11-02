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
/* ItMcsRpc006: rpc diag counters advance across a call
 * PRECONDITION: the master netproxy worker (netproxy_demo) is running. */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    struct mica_rpc_diag before;
    struct mica_rpc_diag after;
    uint8_t req[64];
    uint8_t resp[64];
    uint32_t respLen = 0;
    uint32_t i;
    int ret;

    for (i = 0; i < sizeof(req); i++) {
        req[i] = (uint8_t)(i + 7);
    }
    mica_rpc_get_diag(&before);
    ret = mica_rpc_call(RPC_FUNC_PING, req, sizeof(req),
                        resp, sizeof(resp), &respLen, 3000);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    mica_rpc_get_diag(&after);

    ICUNIT_GOTO_EQUAL(after.rx_cnt > before.rx_cnt, 1, after.rx_cnt, EXIT);
    ICUNIT_GOTO_EQUAL(after.tx_cnt > before.tx_cnt, 1, after.tx_cnt, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsRpc006(VOID)
{
    TEST_ADD_CASE("ItMcsRpc006", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

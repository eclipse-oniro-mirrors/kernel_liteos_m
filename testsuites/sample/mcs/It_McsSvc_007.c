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
/* ItMcsSvc007: mica_send_data (UMT) offset out-of-window validation.
 * Before the fix these calls memcpy'd past the 1MB send window and killed
 * the slave with a data abort (see issue 20261023_164500, It_McsSvc010). */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t buf[16];
    int ret;

    /* exact repro of the reported failure: offset just past the window */
    ret = mica_send_data(buf, 0x100001, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* offset == window size (1MB, UMT_SEND_BUFFER_OFFSET in umt_service.c):
     * also out of range (window end - offset < len) */
    ret = mica_send_data(buf, 0x100000, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);

    /* negative offset must be rejected, never wrapped to a huge size_t */
    ret = mica_send_data(buf, -1, sizeof(buf));
    ICUNIT_GOTO_EQUAL(ret, -22, ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSvc007(VOID)
{
    TEST_ADD_CASE("ItMcsSvc007", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

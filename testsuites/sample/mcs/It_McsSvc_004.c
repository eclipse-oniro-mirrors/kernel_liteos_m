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
/* ItMcsSvc004: mica_tty_send returns the number of bytes sent */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    unsigned char data[16];
    int ret;
    int i;

    for (i = 0; i < (int)sizeof(data); i++) {
        data[i] = (unsigned char)(i + 1);
    }
    ret = mica_tty_send(data, sizeof(data));
    ICUNIT_GOTO_EQUAL(ret, (int)sizeof(data), ret, EXIT);
    return LOS_OK;
EXIT:
    return LOS_NOK;
}

VOID ItMcsSvc004(VOID)
{
    TEST_ADD_CASE("ItMcsSvc004", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

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
/* ItMcsNp005: UDP recvfrom with 200ms timeout returns -EAGAIN (-11)
 * PRECONDITION: the master netproxy worker is running. */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t buf[64];
    uint8_t from[IT_NP_SOCKADDR_LEN];
    uint32_t fromLen = sizeof(from);
    int fd;
    int ret;

    fd = np_socket(2, 2, 0);
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    ret = np_recvfrom(fd, buf, sizeof(buf), 0, 200, from, &fromLen);
    ICUNIT_GOTO_EQUAL(ret, -11, ret, EXIT);
    (void)np_close(fd);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)np_close(fd);
    }
    return LOS_NOK;
}

VOID ItMcsNp005(VOID)
{
    TEST_ADD_CASE("ItMcsNp005", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

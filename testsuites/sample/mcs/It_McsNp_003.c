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
/* ItMcsNp003: TCP connect to a refused port returns -ECONNREFUSED (-111)
 * PRECONDITION: the master netproxy worker is running. */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    int fd;
    int ret;

    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    /* master loopback port 1: nothing listens -> ECONNREFUSED */
    ItNpBuildAddr(addr, 127, 0, 0, 1, 1);
    ret = np_connect(fd, addr, sizeof(addr));
    ICUNIT_GOTO_EQUAL(ret, -111, ret, EXIT);
    (void)np_close(fd);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)np_close(fd);
    }
    return LOS_NOK;
}

VOID ItMcsNp003(VOID)
{
    TEST_ADD_CASE("ItMcsNp003", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

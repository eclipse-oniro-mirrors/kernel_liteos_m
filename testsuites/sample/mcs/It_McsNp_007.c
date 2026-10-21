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
/* ItMcsNp007: TCP echo round-trip through the master stack to the PC echo server
 * PRECONDITION: the master netproxy worker is running. */
#include "It_mcs.h"

static UINT32 Testcase(VOID)
{
    uint8_t addr[IT_NP_SOCKADDR_LEN];
    uint8_t payload[32];
    uint8_t buf[64];
    uint32_t i;
    int fd;
    int ret;

    for (i = 0; i < sizeof(payload); i++) {
        payload[i] = (uint8_t)(i + 5);
    }
    fd = np_socket(2, 1, 0);
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);

    ItNpBuildAddr(addr, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1,
                  IT_NP_ECHO_IP2, IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);
    ret = np_connect(fd, addr, sizeof(addr));
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = np_send(fd, payload, sizeof(payload), 0);
    ICUNIT_GOTO_EQUAL(ret, (int)sizeof(payload), ret, EXIT);

    ret = np_recv(fd, buf, sizeof(buf), 0, 3000);
    ICUNIT_GOTO_EQUAL(ret, (int)sizeof(payload), ret, EXIT);
    ICUNIT_GOTO_EQUAL(memcmp(buf, payload, sizeof(payload)), 0, 1, EXIT);

    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)np_close(fd);
    }
    return LOS_NOK;
}

VOID ItMcsNp007(VOID)
{
    TEST_ADD_CASE("ItMcsNp007", Testcase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

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
#include "it_mcs.h"

/* 用例简要描述: ItMcsNp005 np_select 多路就绪与超时三态及 fd 位图宏 */
static UINT32 Testcase(VOID)
{
    uint8_t echo[IT_NP_SOCKADDR_LEN];
    uint8_t buf[4] = {0x11, 0x22, 0x33, 0x44};
    np_fd_set rd;
    int udp1 = -1;
    int ret;

    ItNpBuildAddr(echo, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1, IT_NP_ECHO_IP2,
                  IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_UDP);
    udp1 = np_socket(2, 2, 0); /* AF_INET, SOCK_DGRAM */
    ICUNIT_GOTO_EQUAL(udp1 >= 0, 1, udp1, EXIT);
    ret = np_sendto(udp1, buf, sizeof(buf), 0, echo, IT_NP_SOCKADDR_LEN);
    ICUNIT_GOTO_EQUAL(ret, 4, ret, EXIT);

    LOS_TaskDelay(60); /* 500Hz tick：约 120ms 等 echo 回包落接收队列 */

    NP_FD_ZERO(&rd);
    NP_FD_SET(udp1, &rd);
    ret = np_select(udp1 + 1, &rd, NULL, NULL, 1000);
    ICUNIT_GOTO_EQUAL(ret >= 1, 1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(NP_FD_ISSET(udp1, &rd), 1, udp1, EXIT);

    NP_FD_CLR(udp1, &rd);
    ICUNIT_GOTO_EQUAL(NP_FD_ISSET(udp1, &rd), 0, 0, EXIT);

    /* nfds=0 且三个集合全空：纯超时等待，应精确返回 0 */
    ret = np_select(0, NULL, NULL, NULL, 200);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = np_close(udp1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    udp1 = -1;
    return LOS_OK;
EXIT:
    if (udp1 >= 0) {
        (void)np_close(udp1);
    }
    return LOS_NOK;
}

VOID ItMcsNp005(VOID)
{
    TEST_ADD_CASE("ItMcsNp005", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL0, TEST_FUNCTION);
}

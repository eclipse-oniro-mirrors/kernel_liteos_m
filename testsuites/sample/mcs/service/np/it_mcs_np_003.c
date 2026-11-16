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

/* 用例简要描述: ItMcsNp003 np_getsockname/np_getpeername 本端对端地址与未连接负例 */
static UINT32 Testcase(VOID)
{
    uint8_t any[IT_NP_SOCKADDR_LEN];
    uint8_t echo[IT_NP_SOCKADDR_LEN];
    uint8_t name[IT_NP_SOCKADDR_LEN];
    uint8_t peer[IT_NP_SOCKADDR_LEN];
    uint32_t nameLen = IT_NP_SOCKADDR_LEN;
    uint32_t peerLen = IT_NP_SOCKADDR_LEN;
    int udp = -1;
    int tcp = -1;
    int udp2 = -1;
    int ret;

    /* UDP bind(INADDR_ANY:0)：内核分配临时端口后 getsockname 应带回非 0 端口 */
    ItNpBuildAddr(any, 0, 0, 0, 0, 0);
    udp = np_socket(2, 2, 0); /* AF_INET, SOCK_DGRAM */
    ICUNIT_GOTO_EQUAL(udp >= 0, 1, udp, EXIT);
    ret = np_bind(udp, any, IT_NP_SOCKADDR_LEN);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = np_getsockname(udp, name, &nameLen);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(nameLen, IT_NP_SOCKADDR_LEN, nameLen, EXIT);
    ICUNIT_GOTO_EQUAL(((name[2] << 8) | name[3]) != 0, 1, (name[2] << 8) | name[3], EXIT);

    /* TCP connect echo 后 getpeername 应回 16 字节 sockaddr */
    ItNpBuildAddr(echo, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1, IT_NP_ECHO_IP2,
                  IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);
    tcp = np_socket(2, 1, 0); /* AF_INET, SOCK_STREAM */
    ICUNIT_GOTO_EQUAL(tcp >= 0, 1, tcp, EXIT);
    ret = np_connect(tcp, echo, IT_NP_SOCKADDR_LEN);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = np_getpeername(tcp, peer, &peerLen);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(peerLen, IT_NP_SOCKADDR_LEN, peerLen, EXIT);

    /* 未连接 UDP：getpeername 无对端可报（主核 ENOTCONN 转回） */
    udp2 = np_socket(2, 2, 0);
    ICUNIT_GOTO_EQUAL(udp2 >= 0, 1, udp2, EXIT);
    peerLen = IT_NP_SOCKADDR_LEN;
    ret = np_getpeername(udp2, peer, &peerLen);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    ret = np_close(tcp);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    tcp = -1;
    ret = np_close(udp);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    udp = -1;
    ret = np_close(udp2);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    udp2 = -1;
    return LOS_OK;
EXIT:
    if (tcp >= 0) {
        (void)np_close(tcp);
    }
    if (udp >= 0) {
        (void)np_close(udp);
    }
    if (udp2 >= 0) {
        (void)np_close(udp2);
    }
    return LOS_NOK;
}

VOID ItMcsNp003(VOID)
{
    TEST_ADD_CASE("ItMcsNp003", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL0, TEST_FUNCTION);
}

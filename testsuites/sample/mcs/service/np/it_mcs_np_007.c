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

/* 用例简要描述: ItMcsNp007 np_shutdown 半关写端后发送失败且不关闭 fd */
static UINT32 Testcase(VOID)
{
    uint8_t echo[IT_NP_SOCKADDR_LEN];
    uint8_t buf[4] = {0x11, 0x22, 0x33, 0x44};
    int fd = -1;
    int ret;

    ItNpBuildAddr(echo, IT_NP_ECHO_IP0, IT_NP_ECHO_IP1, IT_NP_ECHO_IP2,
                  IT_NP_ECHO_IP3, IT_NP_ECHO_PORT_TCP);
    fd = np_socket(2, 1, 0); /* AF_INET, SOCK_STREAM */
    ICUNIT_GOTO_EQUAL(fd >= 0, 1, fd, EXIT);
    ret = np_connect(fd, echo, IT_NP_SOCKADDR_LEN);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = np_shutdown(fd, 1); /* SHUT_WR：只关写端 */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 半关写端后 send 应失败（主核 EPIPE 转回），fd 本身仍可关闭 */
    ret = np_send(fd, buf, sizeof(buf), 0);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);

    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* 关闭后再 close：主核 EBADF 转回（半关不改变 fd 生命周期） */
    ret = np_close(fd);
    ICUNIT_GOTO_EQUAL((ret < 0), 1, ret, EXIT);
    fd = -1;
    return LOS_OK;
EXIT:
    if (fd >= 0) {
        (void)np_close(fd);
    }
    return LOS_NOK;
}

VOID ItMcsNp007(VOID)
{
    TEST_ADD_CASE("ItMcsNp007", Testcase, TEST_LOS, TEST_FN_RK3568SMCS_NPPROXY, TEST_LEVEL0, TEST_FUNCTION);
}

/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"

#define POLL_TEST_BUF_LEN 32
#define POLL_TEST_TIMEOUT 100

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is posix
 * @param        : module name is io
 * @param        : test suit name is PosixIoPollTest
 */
LITE_TEST_SUIT(Posix, Io, PosixIoPollTest);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixIoPollTestSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixIoPollTestTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

#if (LOSCFG_POSIX_PIPE_API == 1)
/* *
 * @tc.number    : TEST_IO_POLL_001
 * @tc.name      : poll reports POLLIN on pipe which has data to read
 * @tc.desc      : [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixIoPollTest, testIoPoll001, Function | MediumTest | Level1)
{
    int fds[2] = {0};
    struct pollfd pfd = {0};
    char writeBuf[] = "poll test data";
    char readBuf[POLL_TEST_BUF_LEN] = {0};
    ssize_t ret;

    ret = pipe(fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = write(fds[1], writeBuf, strlen(writeBuf));
    ICUNIT_ASSERT_EQUAL(ret, (ssize_t)strlen(writeBuf), ret);

    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = poll(&pfd, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret, 1, ret);
    ICUNIT_ASSERT_EQUAL(pfd.revents & POLLIN, POLLIN, pfd.revents);

    ret = read(fds[0], readBuf, POLL_TEST_BUF_LEN);
    ICUNIT_ASSERT_EQUAL(ret, (ssize_t)strlen(writeBuf), ret);

    (void)close(fds[0]);
    (void)close(fds[1]);
    return 0;
}

/* *
 * @tc.number    : TEST_IO_POLL_002
 * @tc.name      : poll returns 0 immediately on empty pipe when timeout is 0
 * @tc.desc      : [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixIoPollTest, testIoPoll002, Function | MediumTest | Level1)
{
    int fds[2] = {0};
    struct pollfd pfd = {0};
    int ret;

    ret = pipe(fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = poll(&pfd, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL(pfd.revents, 0, pfd.revents);

    (void)close(fds[0]);
    (void)close(fds[1]);
    return 0;
}

/* *
 * @tc.number    : TEST_IO_POLL_003
 * @tc.name      : poll returns 0 on empty pipe when timeout expires
 * @tc.desc      : [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixIoPollTest, testIoPoll003, Function | MediumTest | Level1)
{
    int fds[2] = {0};
    struct pollfd pfd = {0};
    int ret;

    ret = pipe(fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    pfd.fd = fds[0];
    pfd.events = POLLIN;
    pfd.revents = 0;
    ret = poll(&pfd, 1, POLL_TEST_TIMEOUT);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ICUNIT_ASSERT_EQUAL(pfd.revents, 0, pfd.revents);

    (void)close(fds[0]);
    (void)close(fds[1]);
    return 0;
}
#endif

/* *
 * @tc.number    : TEST_IO_POLL_004
 * @tc.name      : poll returns -1 and sets errno to EBADF for negative fd
 * @tc.desc      : [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixIoPollTest, testIoPoll004, Function | MediumTest | Level1)
{
    struct pollfd pfd = {0};
    int ret;

    pfd.fd = -1;
    pfd.events = POLLIN;
    pfd.revents = 0;
    errno = 0;
    ret = poll(&pfd, 1, 0);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EBADF, errno);

    return 0;
}

void PosixIoPollFuncTest(void)
{
    LOG("begin PosixIoPollFuncTest....");
#if (LOSCFG_POSIX_PIPE_API == 1)
    RUN_ONE_TESTCASE(testIoPoll001);
    RUN_ONE_TESTCASE(testIoPoll002);
    RUN_ONE_TESTCASE(testIoPoll003);
#endif
    RUN_ONE_TESTCASE(testIoPoll004);
    return;
}

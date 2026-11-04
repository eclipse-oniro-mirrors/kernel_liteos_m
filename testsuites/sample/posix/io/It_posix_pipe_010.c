/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItPosixPipe010
 * 用例描述: poll 参数错误与跨 tick 超时返回
 *
 * 覆盖目标:
 *   lib/posix/src/poll.c:
 *     QueryFds :104-105   nfds!=0 且 fds==NULL → EINVAL
 *     QueryFds :111-112   fd<0 → EBADF
 *     PollTimedWait :196-197  SemPend 超时 → errno=ETIMEDOUT, 返回 0
 *
 * 断言口径:
 *   poll(NULL, 1, 0)              == -1 且 errno==EINVAL
 *   poll(负 fd, 1, 0)             == -1 且 errno==EBADF(POSIX 规定忽略负 fd, 按现实现断言)
 *   空管道 poll(50ms)             == 0(无事件; 50ms=5 tick ≥ 1 tick, 走 SemPend
 *                                  超时分支——现有 003 用 1ms 亚 tick 不可达)
 */

/* 用例简要描述: poll 参数错误与跨 tick 超时返回 */
#include "It_posix_io.h"
#include <poll.h>

static UINT32 Testcase(VOID)
{
    INT32 fds[2] = {0}; /* 2: pipe fd pair */
    INT32 ret;
    struct pollfd pfd;

    /* fds==NULL 且 nfds!=0: QueryFds :104-105 EINVAL */
    errno = 0;
    ret = poll(NULL, 1, 0); /* 1: nfds, 0: no wait */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 负 fd: QueryFds :111-112 EBADF */
    (VOID)memset_s(&pfd, sizeof(pfd), 0, sizeof(pfd));
    pfd.fd = -1;
    pfd.events = POLLIN;
    errno = 0;
    ret = poll(&pfd, 1, 0); /* 1: nfds, 0: no wait */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EBADF, errno);

    /* 空管道跨 tick 超时: PollTimedWait :196-197, 50ms=5 tick */
    ret = pipe(fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    (VOID)memset_s(&pfd, sizeof(pfd), 0, sizeof(pfd));
    pfd.fd = fds[0];
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, 50); /* 1: nfds, 50: timeout ms */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ICUNIT_GOTO_EQUAL(pfd.revents, 0, (UINT32)pfd.revents, EXIT);

EXIT:
    close(fds[0]);
    close(fds[1]);
    return LOS_OK;
}

VOID ItPosixPipe010(VOID)
{
    TEST_ADD_CASE("ItPosixPipe010", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

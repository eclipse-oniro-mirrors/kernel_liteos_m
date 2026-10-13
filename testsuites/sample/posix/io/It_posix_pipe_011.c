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
 * 用例名称: ItPosixPipe011
 * 用例描述: 无限 poll 阻塞被写端数据唤醒
 *
 * 覆盖目标:
 *   lib/posix/src/poll.c:
 *     WaitSemTime :93        timeout==0 参数 → LOS_WAIT_FOREVER 无限等待
 *     PollTimedWait :178-179 timeout<0 → WaitSemTime(0) 挂起, 被写唤醒后 QueryFds 返回 1
 *
 * 断言口径:
 *   主任务 poll(读端, POLLIN, -1) 挂起 → 写者任务写入 8 字节 →
 *   poll 返回 1 且 revents 含 POLLIN
 *
 * 同步: 写者置 g_testCount=1 后让出 2 tick 确保主任务已挂入 poll 等待队列,
 *       唤醒依赖 PollNotify 的 sem post, 不依赖时序硬同步(参照 094 模式)。
 */

/* 用例简要描述: 无限 poll 被写端数据唤醒 */
#include "It_posix_io.h"
#include <poll.h>
#include <pthread.h>

static INT32 g_pipe011Fds[2] = {-1, -1}; /* 2: pipe fd pair */

static VOID *Pipe011Writer(VOID *arg)
{
    CHAR buf[8] = "wakeup"; /* 8: buffer size */
    UINT32 i;

    (VOID)arg;
    /* 等主任务就绪标志(主任务置位后立即进入 poll) */
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_testCount == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    /* 让出 2 tick 确保主任务已真正挂入 poll 等待队列 */
    LOS_TaskDelay(2);
    (VOID)write(g_pipe011Fds[1], buf, sizeof(buf));
    return NULL;
}

static UINT32 Testcase(VOID)
{
    INT32 ret;
    struct pollfd pfd;
    pthread_t writer;

    g_testCount = 0;
    g_pipe011Fds[0] = -1;
    g_pipe011Fds[1] = -1;

    ret = pipe(g_pipe011Fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_create(&writer, NULL, Pipe011Writer, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_CLOSE);

    g_testCount = 1;
    /* 无限 poll: timeout=-1 → WaitSemTime(0)=LOS_WAIT_FOREVER, 无事件则挂起 */
    (VOID)memset_s(&pfd, sizeof(pfd), 0, sizeof(pfd));
    pfd.fd = g_pipe011Fds[0];
    pfd.events = POLLIN;
    ret = poll(&pfd, 1, -1); /* 1: nfds, -1: infinite */
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL((pfd.revents & POLLIN), POLLIN, (UINT32)pfd.revents, EXIT_JOIN);

    ret = pthread_join(writer, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_CLOSE);

EXIT_CLOSE:
    close(g_pipe011Fds[0]);
    close(g_pipe011Fds[1]);
    return LOS_OK;
EXIT_JOIN:
    (VOID)pthread_join(writer, NULL);
    goto EXIT_CLOSE;
}

VOID ItPosixPipe011(VOID)
{
    TEST_ADD_CASE("ItPosixPipe011", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

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
 * 用例名称: ItPosixPipe013
 * 用例描述: 调度锁下管道读写阻塞拒绝
 *
 * 覆盖目标:
 *   lib/posix/src/pipe.c:
 *     PipeRead  :521-522  空管道 LOS_SemPend 失败(调度锁内 PEND_IN_LOCK) → errno=EINVAL
 *     PipeWrite :587-588,600  满管道 LOS_SemPend 失败(调度锁内) → errno=EINVAL
 *
 * 断言口径:
 *   LOS_TaskLock 后: read(空管道) == -1 且 errno==EINVAL;
 *                    write(满管道) == -1 且 errno==EINVAL
 *   解锁后: 正常读写恢复(读存量/写入均成功)
 * 注: 调度锁内失败路径伴随内核 PRINT_ERR 告警(与 It_posix_mutex_041 调度锁先例一致);
 *     本用例写满前无读操作, 读写信号量计数为 0, 不会进入 PIPE_RW_WAIT 自旋。
 */

/* 用例简要描述: 调度锁下管道读写阻塞拒绝 */
#include "It_posix_io.h"

#define PIPE013_BUF_SIZE 1024 /* 1024: PIPE_DEV_BUF_SIZE 环形缓冲容量 */

static UINT32 Testcase(VOID)
{
    INT32 fds[2] = {0}; /* 2: pipe fd pair */
    INT32 ret;
    CHAR wbuf[PIPE013_BUF_SIZE];
    CHAR rbuf[8]; /* 8: small readback buffer */

    ret = pipe(fds);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    (VOID)memset_s(wbuf, sizeof(wbuf), 'a', sizeof(wbuf));

    /* --- 阶段 1: 调度锁内读空管道 → PipeRead :521-522 --- */
    LOS_TaskLock();
    errno = 0;
    ret = read(fds[0], rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_UNLOCK);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT_UNLOCK);
    LOS_TaskUnlock();

    /* --- 阶段 2: 写满 1024 后, 调度锁内写满管道 → PipeWrite :587-588,600 --- */
    ret = write(fds[1], wbuf, PIPE013_BUF_SIZE);
    ICUNIT_GOTO_EQUAL(ret, PIPE013_BUF_SIZE, ret, EXIT);

    LOS_TaskLock();
    errno = 0;
    ret = write(fds[1], rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_UNLOCK);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT_UNLOCK);
    LOS_TaskUnlock();

    /* --- 阶段 3: 解锁后恢复正常读写 --- */
    ret = read(fds[0], rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(rbuf), ret, EXIT);
    ret = write(fds[1], rbuf, sizeof(rbuf));
    ICUNIT_GOTO_EQUAL(ret, sizeof(rbuf), ret, EXIT);

EXIT:
    close(fds[0]);
    close(fds[1]);
    return LOS_OK;
EXIT_UNLOCK:
    LOS_TaskUnlock();
    goto EXIT;
}

VOID ItPosixPipe013(VOID)
{
    TEST_ADD_CASE("ItPosixPipe013", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

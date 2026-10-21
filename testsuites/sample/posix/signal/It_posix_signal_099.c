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
 * 用例名称: ItPosixSignal099
 * 用例描述: pthread_kill 非法任务号与非法信号号拒绝
 *
 * 覆盖目标:
 *   lib/posix/src/signal.c:
 *     pthread_kill :190-191  LOS_SignalSend 失败(任务号越界/信号号非法) →
 *                             errno=EINVAL, return -1
 *
 * 断言口径:
 *   - pthread_kill(999, SIGUSR1) == -1 且 errno==EINVAL(任务号 > TSK_LIMIT)
 *   - pthread_kill(自身合法 id, 999) == -1 且 errno==EINVAL(信号号非法)
 */

/* 用例简要描述: pthread_kill 非法任务号拒绝 */
#include "It_posix_signal.h"
#include <pthread.h>

static UINT32 Testcase(VOID)
{
    pthread_t self = pthread_self();
    INT32 ret;

    /* 非法任务号: LOS_SignalSend(999) 任务号越界 → EINVAL */
    errno = 0;
    ret = pthread_kill((pthread_t)999, SIGUSR1); /* 999: > TSK_LIMIT */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 合法任务号 + 非法信号号: OS_SIGNAL_VALID 拒绝 → EINVAL */
    errno = 0;
    ret = pthread_kill(self, 999); /* 999: > LOS_SIGNAL_SUPPORT_MAX */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    return LOS_OK;
}

VOID ItPosixSignal099(VOID)
{
    TEST_ADD_CASE("ItPosixSignal099", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

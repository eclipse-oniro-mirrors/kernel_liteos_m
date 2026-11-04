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
 * 用例名称: ItPosixTime042
 * 用例描述: nanosleep 超范围与调度锁拒绝
 *
 * 覆盖目标:
 *   lib/posix/src/time.c:
 *     nanosleep :126-127  tick >= UINT32_MAX → EINVAL(tv_sec=INT32_MAX 在
 *                          TICK=100Hz 下换算 2.1e11 tick 越界)
 *     nanosleep :140-141  LOS_TaskDelay 在调度锁内失败 → errno=EINTR
 *
 * 断言口径:
 *   - nanosleep({INT32_MAX, 0}, NULL) == -1 且 errno==EINVAL
 *   - LOS_TaskLock 内 nanosleep({0, 20ms}) == -1 且 errno==EINTR
 *   - 解锁后 nanosleep({0, 20ms}) == 0(恢复)
 */

/* 用例简要描述: nanosleep 超范围与调度锁拒绝 */
#include "It_posix_time.h"

static UINT32 Testcase(VOID)
{
    struct timespec rq = {0};
    INT32 ret;

    /* 1. 超范围: tv_sec=INT32_MAX → tick 2.1e11 >= UINT32_MAX → EINVAL */
    rq.tv_sec = INT32_MAX;
    rq.tv_nsec = 0;
    errno = 0;
    ret = nanosleep(&rq, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 2. 调度锁内: LOS_TaskDelay IN_LOCK → EINTR */
    rq.tv_sec = 0;
    rq.tv_nsec = 20000000; /* 20ms */
    LOS_TaskLock();
    errno = 0;
    ret = nanosleep(&rq, NULL);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_UNLOCK);
    ICUNIT_GOTO_EQUAL(errno, EINTR, errno, EXIT_UNLOCK);
    LOS_TaskUnlock();

    /* 3. 解锁后恢复 */
    ret = nanosleep(&rq, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    return LOS_OK;
EXIT_UNLOCK:
    LOS_TaskUnlock();
    return LOS_NOK;
}

VOID ItPosixTime042(VOID)
{
    TEST_ADD_CASE("ItPosixTime042", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

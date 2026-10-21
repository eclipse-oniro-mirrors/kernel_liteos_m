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
 * 用例名称: ItPosixTime040
 * 用例描述: 定时器设置/查询错误参数矩阵
 *
 * 覆盖目标:
 *   lib/posix/src/time.c:
 *     timer_settime :196-197  flags!=0(TIMER_ABSTIME) → ENOTSUP
 *     timer_settime :215-216  it_interval 与 it_value 不等(均非 0) → ENOTSUP
 *     timer_settime :225-226  LOS_SwtmrStop 非法 ID → EINVAL
 *     timer_gettime :262-263  value==NULL → EINVAL
 *     timer_gettime :271-272  LOS_SwtmrTimeGet 非法 ID → EINVAL
 *
 * 断言口径:
 *   - timer_settime(合法 tid, TIMER_ABSTIME, ...) → -1/ENOTSUP
 *   - timer_settime(合法 tid, 0, {interval≠expiry 均非 0}) → -1/ENOTSUP
 *   - timer_settime(坏 id, 0, 合法 value) → -1/EINVAL
 *   - timer_gettime(合法 tid, NULL) → -1/EINVAL
 *   - timer_gettime(坏 id, &val) → -1/EINVAL
 *   坏 id 用 0x7FFFFFFF(OS_SWT_FROM_SWTID 掩码回卷后 IdVerify 必失败且
 *   不可能与任一槽位现行生成 id 撞车)。
 */

/* 用例简要描述: 定时器设置查询错误参数矩阵 */
#include "It_posix_time.h"

static VOID Time040Callback(union sigval v)
{
    (VOID)v;
}

static UINT32 Testcase(VOID)
{
    timer_t timerID = (timer_t)0;
    struct sigevent evp = {0};
    struct itimerspec val = {0};
    struct itimerspec got = {0};
    INT32 ret;

    evp.sigev_notify = SIGEV_THREAD;
    evp.sigev_notify_function = Time040Callback;
    ret = timer_create(CLOCK_REALTIME, &evp, &timerID);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 1. flags=TIMER_ABSTIME: :196-197 → ENOTSUP(先于 value 校验) */
    val.it_value.tv_sec = 0;
    val.it_value.tv_nsec = 20000000; /* 20ms */
    val.it_interval.tv_sec = 0;
    val.it_interval.tv_nsec = 20000000; /* 20ms */
    errno = 0;
    ret = timer_settime(timerID, TIMER_ABSTIME, &val, NULL);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOTSUP, errno, EXIT);

    /* 2. interval != expiry(均非 0): :215-216 → ENOTSUP */
    val.it_value.tv_nsec = 20000000;    /* 20ms: expiry */
    val.it_interval.tv_nsec = 10000000; /* 10ms: interval ≠ expiry */
    errno = 0;
    ret = timer_settime(timerID, 0, &val, NULL);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOTSUP, errno, EXIT);

    /* 3. 坏 timerID settime: :225-226 → EINVAL */
    val.it_value.tv_nsec = 20000000;
    val.it_interval.tv_nsec = 0;
    errno = 0;
    ret = timer_settime((timer_t)0x7FFFFFFF, 0, &val, NULL);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT);

    /* 4. gettime 出参 NULL: :262-263 → EINVAL */
    errno = 0;
    ret = timer_gettime(timerID, NULL);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT);

    /* 5. 坏 timerID gettime: :271-272 → EINVAL */
    (VOID)memset_s(&got, sizeof(got), 0, sizeof(got));
    errno = 0;
    ret = timer_gettime((timer_t)0x7FFFFFFF, &got);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT);

    ret = timer_delete(timerID);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT:
    (VOID)timer_delete(timerID);
    return LOS_NOK;
}

VOID ItPosixTime040(VOID)
{
    TEST_ADD_CASE("ItPosixTime040", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

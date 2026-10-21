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
 * 用例名称: ItPosixSignal098
 * 用例描述: sigtimedwait 纯超时与非法时间参数(不依赖阻塞唤醒语义)
 *
 * 覆盖目标:
 *   lib/posix/src/signal.c:
 *     sigtimedwait :158-163  负 tv_sec / tv_nsec 越界 → EINVAL(前置防御)
 *     sigtimedwait :163      TimeSpec2Tick(合法 timeout 的换算, static inline 未覆盖)
 *     sigtimedwait :171-176  无 pending + 超时 → LOS_ERRNO_SIGNAL_TIMEOUT → EAGAIN
 *   lib/posix/src/signal.c TimeSpec2Tick :41-51(整函数)
 *
 * 断言口径:
 *   - sigtimedwait(NULL, NULL, &ts) → -1/EINVAL(set==NULL 防御)
 *   - tv_sec=-1 → -1/EINVAL; tv_nsec=1e9 → -1/EINVAL
 *   - 无 pending + 50ms(5 tick) → -1 且 errno==EAGAIN
 *
 * BUG_03 绕开说明: 现有 091(POSIX_BUG_03_SIGTIMEDWAIT 隔离)依赖"阻塞后被信号
 * 唤醒"语义; 本用例走纯超时路径(无 pending、无唤醒依赖, 超时由内核定时唤醒
 * 自身), 语义与误 post 缺陷正交, 风险低(分析材料同判)。
 */

/* 用例简要描述: sigtimedwait 超时与非法时间参数 */
#include "It_posix_signal.h"

static UINT32 Testcase(VOID)
{
    sigset_t set;
    struct timespec ts = {0};
    INT32 ret;

    (VOID)sigemptyset(&set);
    (VOID)sigaddset(&set, SIGUSR1);

    /* 1. set==NULL 防御: :148-151 */
    ts.tv_sec = 0;
    ts.tv_nsec = 50000000; /* 50ms */
    errno = 0;
    ret = sigtimedwait(NULL, NULL, &ts);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 2. 负 tv_sec: :158-163 前置拒绝 */
    ts.tv_sec = -1;
    ts.tv_nsec = 0;
    errno = 0;
    ret = sigtimedwait(&set, NULL, &ts);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 3. tv_nsec 越界(== 1e9): :158-163 */
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000000; /* 1000000000: ns per sec */
    errno = 0;
    ret = sigtimedwait(&set, NULL, &ts);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 4. 无 pending + 50ms 超时: TimeSpec2Tick(:41-51) 换算 + :173-174 → EAGAIN */
    ts.tv_sec = 0;
    ts.tv_nsec = 50000000; /* 50ms = 5 tick(TICK=100Hz), > 1 tick 走真超时 */
    errno = 0;
    ret = sigtimedwait(&set, NULL, &ts);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EAGAIN, errno);

    return LOS_OK;
}

VOID ItPosixSignal098(VOID)
{
    TEST_ADD_CASE("ItPosixSignal098", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

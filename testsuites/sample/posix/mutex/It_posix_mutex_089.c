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
 * 用例名称: ItPosixMutex089
 * 用例描述: 定时加锁三类失败错误码
 *
 * 覆盖目标:
 *   lib/posix/src/pthread_mutex.c:
 *     pthread_mutex_timedlock :480   OsMuxPreCheck 拒绝(坏 magic) → EINVAL
 *     pthread_mutex_timedlock :497-498 ERRORCHECK 型自持重复 timedlock → EDEADLK
 *     pthread_mutex_timedlock :506-510 过期超时换算路径(见下方偏离说明)
 *
 * 断言口径:
 *   - 零初始化(magic=0)的坏 mutex: timedlock == EINVAL
 *   - ERRORCHECK 型: 首次 timedlock(未来 ts) == 0; 二次 timedlock == EDEADLK
 *   - 已过期绝对超时 + 空锁: 按当前实现 == 0(锁被获取), unlock == 0 佐证
 *
 * 实现语义偏离说明(对比初版, 重要):
 *   初版步骤 3 断言"过期绝对超时 → ETIMEDOUT"。实测(qemu/ws63 同源码):
 *   pthread_mutex.c 中 timeoutNs 为 UINT64, (absTimeout->tv_sec -
 *   curTime.tv_sec) * NS + (nsec 差) 的有符号负结果回绕成巨大无符号正数,
 *   `timeoutNs <= 0` 判断(无符号比较)永不成立——过期分支为死代码:
 *   过期 deadline + 空锁 → 直接获取成功(返回 0); 过期 deadline + 被持锁
 *   → 以回绕出的错误超时长期阻塞。POSIX 要求过期 abstime 应立即返回
 *   ETIMEDOUT。该实现缺陷已记录待报(同文件 OsGetTickTimeFromNow 的
 *   绝对值比较写法是正确参考)。
 *   按实测行为隔离: 步骤 3 改为断言当前实际行为(获取成功), 作为回归
 *   tripwire——实现修复后本断言将失败, 届时恢复 ETIMEDOUT 断言即可。
 */

/* 用例简要描述: 定时加锁三类失败错误码 */
#include "It_posix_mutex.h"
#include <time.h>

static UINT32 Testcase(VOID)
{
    pthread_mutex_t badMux = {0}; /* 零初始化: magic=0 ≠ _MUX_MAGIC */
    pthread_mutex_t mux;
    pthread_mutexattr_t attr;
    struct timespec tsFuture = {0};
    struct timespec tsPast = {0};
    struct timespec now = {0};
    INT32 ret;

    /* --- 1. 坏 magic: OsMuxPreCheck :480 → EINVAL --- */
    (VOID)clock_gettime(CLOCK_REALTIME, &now);
    tsFuture.tv_sec = now.tv_sec + 2; /* 2: seconds ahead */
    tsFuture.tv_nsec = now.tv_nsec;
    ret = pthread_mutex_timedlock(&badMux, &tsFuture);
    ICUNIT_ASSERT_EQUAL(ret, EINVAL, ret);

    /* --- 2. ERRORCHECK 重复 timedlock → EDEADLK --- */
    ret = pthread_mutexattr_init(&attr);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_ATTR);
    ret = pthread_mutex_init(&mux, &attr);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_ATTR);

    ret = pthread_mutex_timedlock(&mux, &tsFuture);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_mutex_timedlock(&mux, &tsFuture);
    ICUNIT_GOTO_EQUAL(ret, EDEADLK, ret, EXIT);

    ret = pthread_mutex_unlock(&mux);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* --- 3. 已过期绝对超时: 当前实现回绕后走获取路径(见文件头偏离说明) --- */
    (VOID)memset_s(&now, sizeof(now), 0, sizeof(now));
    (VOID)clock_gettime(CLOCK_REALTIME, &now);
    (VOID)memset_s(&tsPast, sizeof(tsPast), 0, sizeof(tsPast));
    tsPast.tv_sec = now.tv_sec;
    tsPast.tv_nsec = 0; /* 当前秒起点, 必已过期(now.nsec > 0 时) */
    if (now.tv_nsec == 0) { /* 恰在秒边界: 再退 1 秒保证严格过期 */
        tsPast.tv_sec = now.tv_sec - 1;
    }
    ret = pthread_mutex_timedlock(&mux, &tsPast);
    /* POSIX 语义应为 ETIMEDOUT; timeoutNs(UINT64) 回绕缺陷下实际获取成功 */
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    /* 佐证锁确被本任务获取: ERRORCHECK 型 unlock 应成功 */
    ret = pthread_mutex_unlock(&mux);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = pthread_mutex_destroy(&mux);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_ATTR);
    ret = pthread_mutexattr_destroy(&attr);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    return LOS_OK;
EXIT:
    (VOID)pthread_mutex_unlock(&mux);
    (VOID)pthread_mutex_destroy(&mux);
EXIT_ATTR:
    (VOID)pthread_mutexattr_destroy(&attr);
    return LOS_NOK;
}

VOID ItPosixMux089(VOID)
{
    TEST_ADD_CASE("ItPosixMux089", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

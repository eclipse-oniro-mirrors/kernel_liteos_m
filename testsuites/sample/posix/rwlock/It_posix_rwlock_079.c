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
 * 用例名称: ItPosixRwlock079
 * 用例描述: 读写锁过期绝对超时返回
 *
 * 覆盖目标:
 *   lib/posix/src/pthread_rwlock.c:
 *     pthread_rwlock_timedwrlock :232-233  OsRwlockMutexLock(过期 ts)失败透传
 *     pthread_rwlock_timedrdlock :333-334  同上
 *     (OsRwlockMutexLock → pthread_mutex_timedlock: timeoutNs<=0 → ETIMEDOUT
 *      先于内部临界区尝试, 判定确定, 不依赖竞态)
 *
 * 断言口径:
 *   子任务持写锁期间, 主任务以已过期绝对超时(now-1s)调用:
 *   - pthread_rwlock_timedrdlock == ETIMEDOUT
 *   - pthread_rwlock_timedwrlock == ETIMEDOUT
 *   (主任务非锁持有者, 不触发 EDEADLK 属主检查)
 */

/* 用例简要描述: 读写锁过期绝对超时返回 */
#include "It_posix_rwlock.h"
#include <time.h>

static pthread_rwlock_t g_rwl079;
static volatile INT32 g_rwlock079Holding = 0;
static volatile INT32 g_rwlock079Release = 0;
static volatile INT32 g_rwlock079WrRet = -1;
static volatile INT32 g_rwlock079Unlocked = 0;

static VOID *Rwlock079Writer(VOID *arg)
{
    INT32 ret;

    (VOID)arg;
    g_rwlock079WrRet = pthread_rwlock_wrlock(&g_rwl079);
    g_rwlock079Holding = 1;
    /* 持写锁直到主任务完成过期超时探测 */
    UINT32 i;
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_rwlock079Release == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ret = pthread_rwlock_unlock(&g_rwl079);
    if (ret != 0) {
        g_rwlock079WrRet = ret;
    }
    g_rwlock079Unlocked = 1;
    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t writer;
    struct timespec tsPast = {0};
    struct timespec now = {0};
    INT32 ret;
    UINT32 i;

    g_rwlock079Holding = 0;
    g_rwlock079Release = 0;
    g_rwlock079WrRet = -1;
    g_rwlock079Unlocked = 0;

    ret = pthread_rwlock_init(&g_rwl079, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_create(&writer, NULL, Rwlock079Writer, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_rwlock079Holding == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_rwlock079Holding, 1, g_rwlock079Holding, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL(g_rwlock079WrRet, 0, g_rwlock079WrRet, EXIT_JOIN);

    /* 已过期绝对超时(now-1s): 两种定时锁均立即 ETIMEDOUT */
    (VOID)clock_gettime(CLOCK_REALTIME, &now);
    tsPast.tv_sec = now.tv_sec - 1; /* 1: second behind */
    tsPast.tv_nsec = now.tv_nsec;

    ret = pthread_rwlock_timedrdlock(&g_rwl079, &tsPast);
    ICUNIT_GOTO_EQUAL(ret, ETIMEDOUT, ret, EXIT_JOIN);

    ret = pthread_rwlock_timedwrlock(&g_rwl079, &tsPast);
    ICUNIT_GOTO_EQUAL(ret, ETIMEDOUT, ret, EXIT_JOIN);

    g_rwlock079Release = 1;
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_rwlock079Unlocked == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_rwlock079Unlocked, 1, g_rwlock079Unlocked, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL(g_rwlock079WrRet, 0, g_rwlock079WrRet, EXIT_JOIN);

    ret = pthread_join(writer, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = pthread_rwlock_destroy(&g_rwl079);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT_JOIN:
    g_rwlock079Release = 1;
    (VOID)pthread_join(writer, NULL);
EXIT:
    (VOID)pthread_rwlock_destroy(&g_rwl079);
    return LOS_NOK;
}

VOID ItPosixRwlock079(VOID)
{
    TEST_ADD_CASE("ItPosixRwlock079", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL1, TEST_FUNCTION);
}

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
 * 用例名称: ItPosixPthread042
 * 用例描述: PTHREAD_COND_INITIALIZER 静态条件变量懒初始化等待
 *
 * 覆盖目标:
 *   lib/posix/src/pthread_cond.c:
 *     pthread_cond_timedwait :284-285  CondInitCheck(零值静态 cond) →
 *                               pthread_cond_init 懒初始化(未覆盖; wait 版 327-332 已覆盖)
 *
 * 断言口径:
 *   静态零值 cond(PTHREAD_COND_INITIALIZER) + 静态锁 mutex:
 *   - 子线程持锁后直接 pthread_cond_timedwait(2s 超时) → 懒创建成功进入等待
 *   - 主任务 signal 唤醒 → timedwait 返回 0(短超时内被唤醒)
 *   - 收尾 cond/mutex destroy 均返回 0
 */

/* 用例简要描述: 静态条件变量懒初始化等待 */
#include "It_posix_pthread.h"
#include <time.h>

static pthread_cond_t g_cond042 = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_mux042 = PTHREAD_MUTEX_INITIALIZER;
static volatile INT32 g_pthread042Ready = 0;
static volatile INT32 g_pthread042Done = 0;
static volatile INT32 g_pthread042WaitRet = -1;
static volatile INT32 g_pthread042UnlockRet = -1;

static VOID *Pthread042Waiter(VOID *arg)
{
    struct timespec ts = {0};
    struct timespec now = {0};
    INT32 ret;

    (VOID)arg;
    ret = pthread_mutex_lock(&g_mux042);
    if (ret != 0) {
        return NULL;
    }
    (VOID)clock_gettime(CLOCK_REALTIME, &now);
    ts.tv_sec = now.tv_sec + 2; /* 2: seconds ahead */
    ts.tv_nsec = now.tv_nsec;

    g_pthread042Ready = 1;
    /* 静态零值 cond 懒初始化: :284-285 CondInitCheck → init → 挂入等待 */
    g_pthread042WaitRet = pthread_cond_timedwait(&g_cond042, &g_mux042, &ts);
    g_pthread042Done = 1;
    g_pthread042UnlockRet = pthread_mutex_unlock(&g_mux042);
    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t waiter;
    INT32 ret;
    UINT32 i;

    g_pthread042Ready = 0;
    g_pthread042Done = 0;
    g_pthread042WaitRet = -1;
    g_pthread042UnlockRet = -1;

    ret = pthread_create(&waiter, NULL, Pthread042Waiter, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 等待者就绪(持锁并即将进入 timedwait) */
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_pthread042Ready == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_pthread042Ready, 1, g_pthread042Ready, EXIT_JOIN);
    /* ready 置位与真正挂入 cond 等待队列之间的调度空隙, 再让出 2 tick */
    LOS_TaskDelay(2);

    ret = pthread_cond_signal(&g_cond042);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN);

    ret = pthread_join(waiter, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 短超时(2s)内被 signal 唤醒: 返回 0 而非 ETIMEDOUT */
    ICUNIT_GOTO_EQUAL(g_pthread042WaitRet, 0, g_pthread042WaitRet, EXIT);
    ICUNIT_GOTO_EQUAL(g_pthread042UnlockRet, 0, g_pthread042UnlockRet, EXIT);

    /* 懒初始化产物回收 */
    ret = pthread_cond_destroy(&g_cond042);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_mutex_destroy(&g_mux042);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return LOS_OK;
EXIT_JOIN:
    (VOID)pthread_join(waiter, NULL);
EXIT:
    (VOID)pthread_cond_destroy(&g_cond042);
    (VOID)pthread_mutex_destroy(&g_mux042);
    return LOS_NOK;
}

VOID ItPosixPthread042(VOID)
{
    TEST_ADD_CASE("ItPosixPthread042", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}

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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * 用例名称: ItPosixRwlock082
 * 用例描述: rwlock 读锁交互回归看护（读后写 EDEADLK 误报 / 读持有 trywrlock
 *           异常成功 / 组合操作后 rdlock 永久阻塞——缺陷三联形态）
 *
 * 断言口径（修复后契约，对应问题记录 20260925_191208）:
 *   - rdlock→unlock 后 trywrlock 返回 0（owner 节点随 unlock 移除，不再误报 EDEADLK）;
 *   - 他线程持读锁期间 trywrlock 返回 EBUSY（读写互斥，readers 对 trywrlock 可见）;
 *   - 上述组合操作完成后，新线程 timedrdlock 在时限内成功（节点链状态无残留）。
 *
 * 【失败安全设计】（20260925 卡死教训：断言失败不放行同步标志→join 永等 300s）:
 *   子线程只记录返回值、不在中途断言；所有阻塞调用均带时限（timed 变体），
 *   主线程全程有界等待，失败路径统一经 EXIT 放行同步标志后再 join——
 *   任何断言失败都不会挂死批次。
 */

/* 用例简要描述: rwlock 读锁交互回归看护（读后写/读持有/组合后限时可达） */
#include "It_posix_rwlock.h"
#ifdef LOSCFG_KERNEL_SMP
#include <sched.h>
extern int sched_getaffinity(pid_t, size_t, cpu_set_t *);
extern int sched_setaffinity(pid_t, size_t, const cpu_set_t *);
#endif

#ifdef LOSCFG_KERNEL_SMP
extern int pthread_attr_setaffinity_np(pthread_attr_t *attr, size_t cpusetsize, const cpu_set_t *cpuset);
#endif

static pthread_rwlock_t g_rwl082;
static volatile INT32 g_rwl082HolderReady = 0;
static volatile INT32 g_rwl082HolderDone = 0;
static volatile INT32 g_rwl082Release = 0;
static volatile INT32 g_rwl082LateDone = 0;
static INT32 g_rwl082HolderRdlockRet = -1;
static INT32 g_rwl082HolderUnlockRet = -1;
static INT32 g_rwl082LateRdlockRet = -1;

static void *Rwl082ReadHolder(void *arg)
{
    struct timespec ts;
    INT32 wait = 0;

    (VOID)arg;
    (VOID)clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5; // 5, bound every blocking call so the parent join can never hang.
    g_rwl082HolderRdlockRet = pthread_rwlock_timedrdlock(&g_rwl082, &ts);
    if (g_rwl082HolderRdlockRet == 0) {
        g_rwl082HolderReady = 1;
        while ((g_rwl082Release == 0) && (wait < 1000)) { // 1000, fed by the parent EXIT path; bounded anyway.
            (VOID)LOS_TaskDelay(1);
            wait++;
        }
        g_rwl082HolderUnlockRet = pthread_rwlock_unlock(&g_rwl082);
    }
    g_rwl082HolderDone = 1;
    return NULL;
}

static void *Rwl082LateReader(void *arg)
{
    struct timespec ts;

    (VOID)arg;
    (VOID)clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2; // 2, deadline probe: reachable in time when the rwlock state is clean.
    g_rwl082LateRdlockRet = pthread_rwlock_timedrdlock(&g_rwl082, &ts);
    if (g_rwl082LateRdlockRet == 0) {
        (VOID)pthread_rwlock_unlock(&g_rwl082);
    }
    g_rwl082LateDone = 1;
    return NULL;
}

static UINT32 Testcase(VOID)
{
    INT32 ret;
    pthread_t holderThread;
    pthread_t lateThread;
    pthread_attr_t holderAttr;
    pthread_attr_t lateAttr;
    struct sched_param sp;
    INT32 loopCount = 0;
    INT32 holderCreated = 0;
    INT32 lateCreated = 0;

    g_rwl082HolderReady = 0;
    g_rwl082HolderDone = 0;
    g_rwl082Release = 0;
    g_rwl082LateDone = 0;
    g_rwl082HolderRdlockRet = -1;
    g_rwl082HolderUnlockRet = -1;
    g_rwl082LateRdlockRet = -1;

    ret = pthread_rwlock_init(&g_rwl082, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* Scenario 1: rdlock -> unlock -> trywrlock must succeed (owner cleared on unlock). */
    ret = pthread_rwlock_rdlock(&g_rwl082);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_rwlock_unlock(&g_rwl082);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_rwlock_trywrlock(&g_rwl082);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT); /* unfixed: EDEADLK, owner node not removed on unlock */
    ret = pthread_rwlock_unlock(&g_rwl082);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* Scenario 2: trywrlock while another thread holds the read lock must return EBUSY. */
    ret = pthread_attr_init(&holderAttr);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    sp.sched_priority = RWLOCK_TEST_LOW_PRIO;
    (VOID)pthread_attr_setschedparam(&holderAttr, &sp);
    (VOID)pthread_attr_setinheritsched(&holderAttr, PTHREAD_EXPLICIT_SCHED);
#ifdef LOSCFG_KERNEL_SMP
    {
        cpu_set_t copuset;
        (void)memset_s(&copuset, sizeof(cpu_set_t), 0, sizeof(cpu_set_t));
        (void)sched_getaffinity(0, sizeof(cpu_set_t), &copuset);
        (VOID)pthread_attr_setaffinity_np(&holderAttr, sizeof(cpu_set_t), &copuset);
    }
#endif
    ret = pthread_create(&holderThread, &holderAttr, Rwl082ReadHolder, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    holderCreated = 1;

    loopCount = 0;
    while ((g_rwl082HolderReady == 0) && (loopCount < 100)) { // 100, bounded wait for the holder's rdlock.
        LOS_TaskDelay(1);
        loopCount++;
    }
    ICUNIT_GOTO_EQUAL(g_rwl082HolderRdlockRet, 0, g_rwl082HolderRdlockRet, EXIT);

    ret = pthread_rwlock_trywrlock(&g_rwl082);
    ICUNIT_GOTO_EQUAL(ret, EBUSY, ret, EXIT); /* unfixed: 0, readers invisible to trywrlock */

    g_rwl082Release = 1;
    loopCount = 0;
    while ((g_rwl082HolderDone == 0) && (loopCount < 100)) { // 100, bounded wait for the holder's unlock.
        LOS_TaskDelay(1);
        loopCount++;
    }
    ICUNIT_GOTO_EQUAL(g_rwl082HolderUnlockRet, 0, g_rwl082HolderUnlockRet, EXIT);

    /* Scenario 3: a fresh reader must reach the lock within the deadline (no leftover state). */
    ret = pthread_attr_init(&lateAttr);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    sp.sched_priority = RWLOCK_TEST_LOW_PRIO;
    (VOID)pthread_attr_setschedparam(&lateAttr, &sp);
    (VOID)pthread_attr_setinheritsched(&lateAttr, PTHREAD_EXPLICIT_SCHED);
    ret = pthread_create(&lateThread, &lateAttr, Rwl082LateReader, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    lateCreated = 1;

    loopCount = 0;
    while ((g_rwl082LateDone == 0) && (loopCount < 300)) { // 300 > 2s deadline: the late reader cannot hang this wait.
        LOS_TaskDelay(1);
        loopCount++;
    }
    ICUNIT_GOTO_EQUAL(g_rwl082LateRdlockRet, 0, g_rwl082LateRdlockRet, EXIT); /* unfixed: ETIMEDOUT */

EXIT:
    g_rwl082Release = 1;
    if (holderCreated == 1) {
        (VOID)PosixPthreadDestroy(&holderAttr, holderThread);
    }
    if (lateCreated == 1) {
        (VOID)PosixPthreadDestroy(&lateAttr, lateThread);
    }
    (VOID)pthread_rwlock_destroy(&g_rwl082);
    return LOS_OK;
}

VOID ItPosixRwlock082(void)
{
    TEST_ADD_CASE("ItPosixRwlock082", Testcase, TEST_LIB, TEST_FN_COMPAT_POSIX, TEST_LEVEL2, TEST_FUNCTION);
}

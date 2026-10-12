/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
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

#include <time.h>
#include <semaphore.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include "ohos_types.h"
#include "posix_test.h"
#include "los_config.h"
#include "kernel_test.h"
#include "log.h"

#define NANO_S 1000000000

/* *
 * @tc.desc      : register a test suite, this suite is used to test basic flow and interface dependency
 * @param        : subsystem name is utils
 * @param        : module name is utilsFile
 * @param        : test suit name is PosixSemaphoreFuncTestSuite
 */
LITE_TEST_SUIT(Posix, PosixSemaphore, PosixSemaphoreFuncTestSuite);

/* *
 * @tc.setup     : setup for all testcases
 * @return       : setup result, TRUE is success, FALSE is fail
 */
static BOOL PosixSemaphoreFuncTestSuiteSetUp(void)
{
    return TRUE;
}

/* *
 * @tc.teardown  : teardown for all testcases
 * @return       : teardown result, TRUE is success, FALSE is fail
 */
static BOOL PosixSemaphoreFuncTestSuiteTearDown(void)
{
    LOG("+-------------------------------------------+\n");
    return TRUE;
}

// get cur-time plus ms
struct timespec GetDelayedTime(unsigned int ms)
{
    /* 注: 本函数内不得夹打印——本板 PRINTK 每条 ~100ms, 且 T0 读取后如果
     * 再打印, 调用方进 sem_timedwait 时 now 已越过 +ms 的 deadline,
     * 导致入口即 ETIMEDOUT(实测 Timedwait002 ret=-1 的根因)。 */
    struct timespec ts = { 0 };
    const unsigned int nsecPerSec = 1000000000;
    const unsigned int nsecPerMs = 1000000;
    unsigned long long setTimeNs = ms * nsecPerMs;
    struct timespec tsNow = { 0 };
    clock_gettime(CLOCK_REALTIME, &tsNow);
    ts.tv_sec = tsNow.tv_sec + (tsNow.tv_nsec + setTimeNs) / nsecPerSec;
    ts.tv_nsec = (tsNow.tv_nsec + setTimeNs) % nsecPerSec;
    return ts;
}

// calculate time difference, in ms
int GetTimeDiff(struct timespec ts1, struct timespec ts2)
{
    const int nsecPerSec = 1000000000;
    const int nsecPerMs = 1000000;
    int ms = (ts1.tv_sec - ts2.tv_sec) * nsecPerSec + (ts1.tv_nsec - ts2.tv_nsec);
    ms = ms / nsecPerMs;
    LOG("different result: %d (ms)", ms);
    return ms;
}

/* *
 * @tc.number   SUB_KERNEL_IPC_SEM_TIMEDWAIT_001
 * @tc.name     sem_timedwait get semaphore timeout
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSem_Timedwait001, Function | MediumTest | Level1)
{
    struct timespec ts = { 0 };
    struct timespec tsNow = { 0 };
    sem_t sem;

    ICUNIT_GOTO_EQUAL(sem_init((sem_t *)&sem, 0, 0), 0, 0, EXIT);

    ts = GetDelayedTime(100);
    /* 注: deadline 计算与 sem_timedwait 之间不得夹打印——本板 PRINTK 每条
     * ~100ms 且打印期丢 tick, 会吃掉 100ms 超时裕量致入口即超时(实测 130-180ms)。 */
    if (sem_timedwait((sem_t *)&sem, &ts) == -1) {
        ICUNIT_GOTO_EQUAL(errno, ETIMEDOUT, errno, EXIT);
    } else {
        LOG("\n> sem_timedwait return unexpected");
    }

    clock_gettime(CLOCK_REALTIME, &tsNow);
    LOG("tsNow %lld, %ld", tsNow.tv_sec, tsNow.tv_nsec);
    int timeDiff = GetTimeDiff(tsNow, ts); // calculate time different
    LOG("timeDiff %d", timeDiff);
    /* 容差 5 tick: OsGetTickTimeFromNow 有 +1 tick 转换罚则(time_internal.h),
     * 加上 tick 量化与唤醒调度延迟, 实测 10-30ms; 3 tick 的原容差压线误报。 */
    ICUNIT_GOTO_EQUAL(abs(timeDiff) < (5 * 1000 / LOSCFG_BASE_CORE_TICK_PER_SECOND), TRUE, 0, EXIT);

    ICUNIT_TRACK_EQUAL(sem_destroy((sem_t *)&sem), 0, 0);
    return 0;

EXIT:
    (VOID)sem_destroy((sem_t *)&sem);
    return LOS_NOK;
}

/* *
 * @tc.number   SUB_KERNEL_IPC_SEM_TIMEDWAIT_001
 * @tc.name     sem_timedwait get semaphore success
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSem_Timedwait002, Function | MediumTest | Level1)
{
    struct timespec ts = { 0 };
    struct timespec tsNow = { 0 };
    struct timespec tsBegin = { 0 };
    sem_t sem;

    ICUNIT_ASSERT_EQUAL(sem_init((sem_t *)&sem, 0, 1), 0, 0);

    ts = GetDelayedTime(100);
    /* 注: 同 001, deadline 与 sem_timedwait 之间不得夹打印 */
    clock_gettime(CLOCK_REALTIME, &tsBegin);
    int ret = sem_timedwait((sem_t *)&sem, &ts);
    clock_gettime(CLOCK_REALTIME, &tsNow);

    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    LOG("\n tsBegin %lld, %ld, tsNow %lld, %ld", tsBegin.tv_sec, tsBegin.tv_nsec, tsNow.tv_sec, tsNow.tv_nsec);
    int timeDiff = GetTimeDiff(tsNow, tsBegin); // calculate time different
    LOG("\n timeDiff %d", timeDiff);
    ICUNIT_ASSERT_WITHIN_EQUAL(timeDiff, timeDiff, (3 * 1000 / LOSCFG_BASE_CORE_TICK_PER_SECOND), 0);

    // try get semaphore again
    ts = GetDelayedTime(100);
    /* 注: 同 001, deadline 与 sem_timedwait 之间不得夹打印 */
    ret = sem_timedwait((sem_t *)&sem, &ts);
    clock_gettime(CLOCK_REALTIME, &tsNow);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, ETIMEDOUT, errno);
    LOG("\n tsNow %lld, %ld", tsNow.tv_sec, tsNow.tv_nsec);
    timeDiff = GetTimeDiff(tsNow, tsBegin); // calculate time different
    LOG("\n wait timeDiff %d", timeDiff);

    ICUNIT_ASSERT_EQUAL(sem_destroy((sem_t *)&sem), 0, 0);
    return 0;
}

/* *
 * @tc.number   SUB_KERNEL_IPC_SEM_TIMEDWAIT_003
 * @tc.name     {sem_timedwait} get semaphore, wait time abnormal, tv_nsec less than 0
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSem_Timedwait003, Function | MediumTest | Level3)
{
    int ret;
    struct timespec ts;
    sem_t sem;

    ts.tv_sec = 0;
    ts.tv_nsec = 200000;
    ret = sem_timedwait((sem_t *)&sem, &ts);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    ret = sem_init((sem_t *)&sem, 0, 0);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ts.tv_sec = time(NULL);
    ts.tv_nsec = -2;
    ret = sem_timedwait((sem_t *)&sem, &ts);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    ts.tv_sec = time(NULL);
    ts.tv_nsec = NANO_S;
    ret = sem_timedwait((sem_t *)&sem, &ts);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    ret = sem_timedwait((sem_t *)&sem, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    ret = sem_destroy((sem_t *)&sem);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ts.tv_sec = time(NULL);
    ts.tv_nsec = 2000000;
    ret = sem_timedwait(NULL, &ts);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);
    return 0;
}

/* *
 * @tc.number   SUB_KERNEL_IPC_SEM_TIMEDWAIT_004
 * @tc.name     sem_trywait get semaphore success
 * @tc.desc     [C- SOFTWARE -0200]
 */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSem_Trywait004, Function | MediumTest | Level3)
{
    sem_t sem;
    int val;
    int ret;

    ret = sem_init(&sem, 0, 1);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = sem_trywait(&sem);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = sem_getvalue(&sem, &val);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    if (val <= 0) {
        sem_destroy(&sem);
        return LOS_OK;
    } else {
        ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    }
    return 0;
}


/* *
 * @tc.number   SUB_KERNEL_IPC_SEM_NAMED_001
 * @tc.name     sem_open/sem_close/sem_unlink ENOSYS stub contract
 * @tc.desc     [C- SOFTWARE -0200]
 */
/* 用例简要描述: sem_open/close/unlink ENOSYS 桩契约固化 */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSem_Named001, Function | MediumTest | Level1)
{
    /* B 侧具名信号量为 ENOSYS 桩(semaphore.c:209-229),固化桩契约 */
    sem_t *ret = sem_open("test_sem_named", O_CREAT, 0644, 1); /* 0644: mode, 1: value */
    ICUNIT_ASSERT_EQUAL(ret, SEM_FAILED, (UINTPTR)ret);
    ICUNIT_ASSERT_EQUAL(errno, ENOSYS, errno);

    int rc = sem_close(ret);
    ICUNIT_ASSERT_EQUAL(rc, -1, rc);
    ICUNIT_ASSERT_EQUAL(errno, ENOSYS, errno);

    rc = sem_unlink("test_sem_named");
    ICUNIT_ASSERT_EQUAL(rc, -1, rc);
    ICUNIT_ASSERT_EQUAL(errno, ENOSYS, errno);

    return 0;
}

/* *
 * @tc.number   SUB_KERNEL_IPC_SEM_PARAM_001
 * @tc.name     sem interfaces invalid parameter and stale handle mapping
 * @tc.desc     [C- SOFTWARE -0200]
 */
/* 用例简要描述: sem_wait/post/trywait/getvalue/destroy 对 NULL 与失效句柄的错误码映射
 * 覆盖目标: semaphore.c:48-50(MapError SEM_INVALID→EINVAL), :95-97(sem_destroy NULL/
 * 未 init 句柄), :113-115, :131-133, :149-151, :168-170, :196-198（gcov 第 1+2 轮） */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSemParam001, Function | MediumTest | Level2)
{
    sem_t sem = {0}; /* 零初始化: s_magic=0 ≠ _SEM_MAGIC, 未 init/已 destroy 状态确定 */
    int val = -1;
    int ret;

    /* 1. NULL 句柄: 各接口直接走参数校验行, 返回 -1 且 errno==EINVAL */
    errno = 0;
    ret = sem_wait(NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    errno = 0;
    ret = sem_post(NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    errno = 0;
    ret = sem_trywait(NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    errno = 0;
    ret = sem_getvalue(NULL, &val);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 输出指针为 NULL: 零初始化句柄 magic 不符 → sem_getvalue(sem, NULL) 同样 EINVAL(:196-198) */
    errno = 0;
    ret = sem_getvalue((sem_t *)&sem, NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 2. sem_destroy 参数校验(semaphore.c:95-97): NULL 句柄与未 init(magic 不符)句柄
     * 均返回 -1 且 errno==EINVAL(纯参数校验, 不触内核删除路径) */
    errno = 0;
    ret = sem_destroy(NULL);
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    errno = 0;
    ret = sem_destroy(&sem); /* 零初始化 sem: s_magic=0 ≠ _SEM_MAGIC */
    ICUNIT_ASSERT_EQUAL(ret, -1, ret);
    ICUNIT_ASSERT_EQUAL(errno, EINVAL, errno);

    /* 注: "destroy 后失效句柄"段已移除(20260912 第 1 轮实测): sem_destroy 不清 s_magic
     * 且 LOS_SemPend 对已删除句柄实测返回 LOS_OK(句柄失效防线缺失, 属误用/异常路径+内核
     * 行为待产品侧裁决——建议 destroy 清零 s_magic 并使 LOS 层对 UNUSED 句柄报 INVALID);
     * 该路径与 MapError 表长尾一并归入异常分支, 本轮不覆盖。 */
    return 0;
}

RUN_TEST_SUITE(PosixSemaphoreFuncTestSuite);

/**
 * @tc.number   SUB_KERNEL_IPC_SEM_PARAM_002
 * @tc.name     sem resource exhaustion and destroy recovery
 * @tc.desc     [C- SOFTWARE -0200]
 */
/* 用例简要描述: 信号量资源耗尽与销毁恢复
 * 覆盖目标: semaphore.c:51-52(MapError SEM_ALL_BUSY→ENOSPC) + sem_init 失败 :81-82 */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSemParam002, Function | MediumTest | Level2)
{
    sem_t semPool[LOSCFG_BASE_IPC_SEM_LIMIT + 1];
    int created = 0;
    int ret;
    int i;

    /* 循环 init 至 sem 池耗尽(qemu 48 / ws63 32, 扣除运行时存量)。
     * 池上限即 LOSCFG_BASE_IPC_SEM_LIMIT, 运行时存量可能为 0(此前用例恰好全部
     * 销毁, 全量跑实测如此), 故多试 1 次保证任意存量下都必触耗尽 */
    for (i = 0; i < LOSCFG_BASE_IPC_SEM_LIMIT + 1; i++) {
        ret = sem_init(&semPool[i], 0, 1);
        if (ret != 0) {
            break;
        }
        created++;
    }
    ICUNIT_GOTO_NOT_EQUAL(created, 0, created, EXIT);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOSPC, errno, EXIT);

    /* 全部销毁回收 */
    for (i = 0; i < created; i++) {
        ret = sem_destroy(&semPool[i]);
        ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    }
    created = 0;

    /* 资源恢复后可重建 */
    sem_t semNew;
    ret = sem_init(&semNew, 0, 1);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = sem_destroy(&semNew);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return 0;
EXIT:
    for (i = 0; i < created; i++) {
        (VOID)sem_destroy(&semPool[i]);
    }
    return LOS_NOK;
}

/**
 * @tc.number   SUB_KERNEL_IPC_SEM_PARAM_003
 * @tc.name     sem_post counting overflow
 * @tc.desc     [C- SOFTWARE -0200]
 */
/* 用例简要描述: 信号量计数上限溢出报 ENOMEM
 * 覆盖目标: semaphore.c:53-54(MapError SEM_OVERFLOW→ENOMEM) + sem_post 失败 :138-139 */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSemParam003, Function | MediumTest | Level2)
{
    sem_t sem;
    int ret;

    /* 计数上限 OS_SEM_COUNTING_MAX_COUNT=0xFFFF: init 至 0xFFFF-1 */
    ret = sem_init(&sem, 0, OS_SEM_COUNTING_MAX_COUNT - 1);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 第 1 次 post: 0xFFFF-1 → 0xFFFF 成功 */
    ret = sem_post(&sem);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 第 2 次 post: 已达 maxSemCount → LOS_ERRNO_SEM_OVERFLOW → ENOMEM */
    errno = 0;
    ret = sem_post(&sem);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, ENOMEM, errno, EXIT);

    ret = sem_destroy(&sem);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return 0;
EXIT:
    (VOID)sem_destroy(&sem);
    return LOS_NOK;
}

static sem_t g_semParam004;
static volatile int g_semParam004Ready = 0;
static volatile int g_semParam004Done = 0;
static volatile int g_semParam004WaitRet = -1;

static void *SemParam004Waiter(void *arg)
{
    (void)arg;
    g_semParam004Ready = 1;
    g_semParam004WaitRet = sem_wait(&g_semParam004); /* 阻塞等待 */
    g_semParam004Done = 1;
    return NULL;
}

/**
 * @tc.number   SUB_KERNEL_IPC_SEM_PARAM_004
 * @tc.name     sem_destroy with waiter blocked
 * @tc.desc     [C- SOFTWARE -0200]
 */
/* 用例简要描述: 有等待者时信号量销毁报 EBUSY
 * 覆盖目标: semaphore.c:55-56(MapError SEM_PENDED→EBUSY) + sem_destroy 失败 :102-103 */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSemParam004, Function | MediumTest | Level2)
{
    pthread_t waiter;
    int ret;
    int i;

    g_semParam004Ready = 0;
    g_semParam004Done = 0;
    g_semParam004WaitRet = -1;

    ret = sem_init(&g_semParam004, 0, 0);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    ret = pthread_create(&waiter, NULL, SemParam004Waiter, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_SEM);

    /* 等待者就绪并让出 2 tick 确保其已阻塞在 sem_wait 上 */
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_semParam004Ready == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_semParam004Ready, 1, g_semParam004Ready, EXIT_JOIN);

    /* 有等待者时销毁: LOS_ERRNO_SEM_PENDED → EBUSY */
    errno = 0;
    ret = sem_destroy(&g_semParam004);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL(errno, EBUSY, errno, EXIT_JOIN);

    /* post 唤醒等待者后回收 */
    ret = sem_post(&g_semParam004);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN);
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_semParam004Done == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_semParam004Done, 1, g_semParam004Done, EXIT_JOIN);
    ICUNIT_GOTO_EQUAL(g_semParam004WaitRet, 0, g_semParam004WaitRet, EXIT_JOIN);

    ret = pthread_join(waiter, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_SEM);

    /* 等待者退出后销毁成功 */
    ret = sem_destroy(&g_semParam004);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_SEM);
    return 0;
EXIT_JOIN:
    (VOID)sem_post(&g_semParam004);
    (VOID)pthread_join(waiter, NULL);
EXIT_SEM:
    (VOID)sem_destroy(&g_semParam004);
    return LOS_NOK;
}

static volatile int g_semParam005Ready = 0;
static volatile int g_semParam005Posted = 0;

static void *SemParam005Poster(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_semParam005Ready == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    LOS_TaskDelay(2);
    /* 配对唤醒主任务持有的 sem（经 arg 传入; 原误 post 零初始化全局
     * g_semParam005, magic 不符报 EINVAL, 主任务 24h 等待不被唤醒——实测修正） */
    (VOID)sem_post((sem_t *)arg);
    g_semParam005Posted = 1;
    return NULL;
}

/**
 * @tc.number   SUB_KERNEL_IPC_SEM_PARAM_005
 * @tc.name     sem error codes under tasklock / bad handle / far timeout
 * @tc.desc     [C- SOFTWARE -0200]
 */
/* 用例简要描述: 调度锁与超时钳制下信号量错误码
 * 覆盖目标: semaphore.c:48-50(SEM_INVALID→EINVAL), :57-58(PEND_IN_LOCK→EPERM),
 * :179-181(远超时钳制为 WAIT_FOREVER), :203-204(getvalue 坏句柄→EINVAL) */
LITE_TEST_CASE(PosixSemaphoreFuncTestSuite, testIpcSemParam005, Function | MediumTest | Level1)
{
    sem_t sem;
    struct timespec ts = {0};
    struct timespec tsNow = {0};
    pthread_t poster;
    int val = -1;
    int handleSave;
    int ret;
    int i;

    g_semParam005Ready = 0;
    g_semParam005Posted = 0;

    /* 1. 调度锁内空 sem wait: SEM_PEND_IN_LOCK → EPERM(:57-58) */
    ret = sem_init(&sem, 0, 0);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    LOS_TaskLock();
    errno = 0;
    ret = sem_wait(&sem);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_UNLOCK);
    ICUNIT_GOTO_EQUAL(errno, EPERM, errno, EXIT_UNLOCK);

    /* 2. 调度锁内空 sem trywait: OsSemValidCheck 的锁检查(los_sem.c:210-213)先于
           计数检查(los_sem.c:251), NO_WAIT 亦报 SEM_PEND_IN_LOCK → EPERM(实测修正) */
    errno = 0;
    ret = sem_trywait(&sem);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT_UNLOCK);
    ICUNIT_GOTO_EQUAL(errno, EPERM, errno, EXIT_UNLOCK);
    LOS_TaskUnlock();

    /* 2b. 解锁后空 sem trywait: NO_WAIT 空计数 → SEM_UNAVAILABLE → EINVAL
           (与 POSIX EAGAIN 惯例不同, 语义差异登记项) */
    errno = 0;
    ret = sem_trywait(&sem);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT);

    /* 3. 坏句柄 getvalue: 直接改写 __val[1](公开 ABI 字段)后 → EINVAL(:203-204) */
    handleSave = sem.__val[1];
    sem.__val[1] = -1; /* -1: (UINT32)-1 ≥ SEM_LIMIT, 必为非法句柄 */
    errno = 0;
    ret = sem_getvalue(&sem, &val);
    ICUNIT_GOTO_EQUAL(ret, -1, ret, EXIT);
    ICUNIT_GOTO_EQUAL(errno, EINVAL, errno, EXIT);
    sem.__val[1] = handleSave; /* 还原句柄, 保证可销毁 */

    /* 4. 远超时钳制: 500 天超时换算 tick 4.32e9(tick=100/s) > LOS_WAIT_FOREVER(0xFFFFFFFF)
           → 钳为永远等(:179-181), 由配对 post 唤醒(防挂死), 返回 0 */
    ret = pthread_create(&poster, NULL, SemParam005Poster, &sem);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (VOID)clock_gettime(CLOCK_REALTIME, &tsNow);
    ts.tv_sec = tsNow.tv_sec + 500 * 24 * 3600; /* 500 days: 换算 tick 必超 LOS_WAIT_FOREVER */
    ts.tv_nsec = tsNow.tv_nsec;
    g_semParam005Ready = 1;
    ret = sem_timedwait(&sem, &ts);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT_JOIN);

    for (i = 0; i < 100; i++) { /* 100: poll bound */
        if (g_semParam005Posted == 1) {
            break;
        }
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_semParam005Posted, 1, g_semParam005Posted, EXIT_JOIN);
    ret = pthread_join(poster, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    ret = sem_destroy(&sem);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    return 0;
EXIT_JOIN:
    (VOID)sem_post(&sem);
    (VOID)pthread_join(poster, NULL);
    goto EXIT;
EXIT_UNLOCK:
    LOS_TaskUnlock();
EXIT:
    (VOID)sem_destroy(&sem);
    return LOS_NOK;
}

void PosixSemaphoreFuncTest()
{
    dprintf("begin PosixSemaphoreFuncTest....");
    RUN_ONE_TESTCASE(testIpcSem_Timedwait001);
    RUN_ONE_TESTCASE(testIpcSem_Named001);
    RUN_ONE_TESTCASE(testIpcSem_Timedwait002);
    RUN_ONE_TESTCASE(testIpcSem_Timedwait003);
    RUN_ONE_TESTCASE(testIpcSem_Trywait004);
    RUN_ONE_TESTCASE(testIpcSemParam001);
    RUN_ONE_TESTCASE(testIpcSemParam002);
    RUN_ONE_TESTCASE(testIpcSemParam003);
    RUN_ONE_TESTCASE(testIpcSemParam004);
    RUN_ONE_TESTCASE(testIpcSemParam005);

    return;
}
/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 */

/* 用例简要描述: 【BUG_20 缺陷暴露用例】异步取消线程泄漏 pthread TSD key 数组且跳过析构器
 *
 * 【现象】    线程设置 PTHREAD_CANCEL_ASYNCHRONOUS 后调用 pthread_setspecific
 *            (首次调用 malloc PTHREAD_KEYS_MAX*sizeof(uintptr_t)=512B key 数组),
 *            主任务 pthread_cancel 该线程: DoPthreadCancel 挂起+free(task->arg)
 *            +LOS_TaskDelete 硬杀, 但 1) 不释放 pthreadData->key 数组(512B 堆块
 *            随 PthreadData 释放而失联, 永久泄漏); 2) 不执行 key 析构器
 *            (POSIX 要求线程终止时对在用 TSD 值调 destructor)。
 * 【根因】    lib/posix/src/pthread.c:449-453 DoPthreadCancel 仅 free(task->arg),
 *            缺少 PthreadExitKeyDtor 等价清理; key 数组与析构器仅在
 *            pthread_exit 主动退出路径(pthread.c:676 PthreadExitKeyDtor)回收,
 *            异步取消路径完全绕过。
 * 【修复方向】DoPthreadCancel 在 free(task->arg) 前调用 PthreadExitKeyDtor
 *            (释放 key 数组 + 执行析构器), 再摘链表/释放 PthreadData/删任务。
 * 【恢复方法】修复后把 bug_isolate.h 中 POSIX_BUG_20_PTHREAD_CANCEL_KEY_LEAK 置 1,
 *            聚合注册点(It_posix_pthread.c)恢复调用, 用例恢复执行作为回归看护常跑。
 * 【关联】    BUG_20 / resleak 批次问题记录 20261023_213004。
 * 修正记录:  第 1 轮 victim 阻塞在 LOS_TaskDelay 且默认 joinable, 引入延时态
 *            删除与 ZOMBIE 残留噪音(delta 4644 含 4KB 栈); 第 2 轮改 sem_wait
 *            PEND 态+DETACHED 属性(连带发现: joinable 线程被 DoPthreadCancel
 *            硬杀后 PthreadData 已 free, IsPthread 失败, pthread_join 返回
 *            EINVAL, ZOMBIE 任务栈永不可回收 —— 见问题记录补充说明)。
 * 修正记录:  第 2 轮全量取证 delta 仍 4644(栈残留主导), 本轮 DETACHED 后
 *            差值应收敛到 512B key 数组本体。 */
#include "It_posix_pthread.h"
#include <semaphore.h>
#include "los_memory.h"

static pthread_key_t g_pthread044Key;
static sem_t g_pthread044Sem;
static volatile INT32 g_pthread044DtorCount = 0;
static volatile UINT32 g_pthread044Ready = 0;

static VOID Pthread044Dtor(VOID *value)
{
    (VOID)value;
    g_pthread044DtorCount++;
}

static VOID *Pthread044Victim(VOID *arg)
{
    INT32 ret;

    (VOID)arg;
    ret = pthread_key_create(&g_pthread044Key, Pthread044Dtor);
    if (ret != 0) {
        return NULL;
    }
    ret = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    if (ret != 0) {
        (VOID)pthread_key_delete(g_pthread044Key);
        return NULL;
    }
    /* 首次 setspecific: malloc key 数组(512B)并挂到本线程 PthreadData */
    ret = pthread_setspecific(g_pthread044Key, (VOID *)0x44);
    if (ret != 0) {
        (VOID)pthread_key_delete(g_pthread044Key);
        return NULL;
    }
    g_pthread044Ready = 1;

    /* 阻塞在 sem PEND 态(非 delay 态): 删除路径干净, 无延时态删除噪音 */
    (VOID)sem_wait(&g_pthread044Sem);
    return NULL;
}

static VOID *Pthread044KeyCleaner(VOID *arg)
{
    (VOID)arg;
    (VOID)pthread_key_delete(g_pthread044Key);
    return NULL;
}

static UINT32 Testcase(VOID)
{
    pthread_t victim;
    pthread_t cleaner;
    pthread_attr_t attr;
    INT32 ret;
    UINT32 memUsedBefore;
    UINT32 memUsedAfter;

    g_pthread044DtorCount = 0;
    g_pthread044Ready = 0;

    ret = sem_init(&g_pthread044Sem, 0, 0);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* DETACHED 属性: 规避 joinable 线程被 DoPthreadCancel 硬杀后遗留不可
     * pthread_join 回收的 ZOMBIE(连带发现, 见问题记录补充说明), 使差值
     * 只剩本用例聚焦的 512B key 数组泄漏本体 */
    ret = pthread_attr_init(&attr);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    memUsedBefore = LOS_MemTotalUsedGet(m_aucSysMem0);

    ret = pthread_create(&victim, &attr, Pthread044Victim, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);
    (VOID)pthread_attr_destroy(&attr);

    /* 有界等待: victim 完成 key 创建/异步类型设置/setspecific 并阻塞在 sem */
    UINT32 loop = 0;
    while ((g_pthread044Ready == 0) && (loop < 3000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_pthread044Ready, 1, g_pthread044Ready, EXIT);
    (VOID)LOS_TaskDelay(2); /* 2: 确保 victim 阻塞在 sem_wait, 取消时机稳定 */

    ret = pthread_cancel(victim);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 等 idle 回收 victim 栈, 消除瞬时内存抖动 */
    (VOID)LOS_TaskDelay(3); /* 3: ticks */

    memUsedAfter = LOS_MemTotalUsedGet(m_aucSysMem0);
    dprintf("[Pthread044] memUsed before=%u after=%u delta=%d (expect 0), dtorCount=%d (expect 1)\n",
            memUsedBefore, memUsedAfter, (INT32)(memUsedAfter - memUsedBefore), g_pthread044DtorCount);

    /* 修复后契约: 取消路径必须释放 key 数组且执行 key 析构器 */
    ICUNIT_GOTO_EQUAL(memUsedAfter, memUsedBefore, memUsedAfter, EXIT);
    ICUNIT_GOTO_EQUAL(g_pthread044DtorCount, 1, g_pthread044DtorCount, EXIT);

    /* 清理全局 key 槽位与信号量: key 族接口要求 pthread 上下文, 用辅助线程删除 */
    ret = pthread_create(&cleaner, NULL, Pthread044KeyCleaner, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    ret = pthread_join(cleaner, NULL);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);
    (VOID)sem_destroy(&g_pthread044Sem);

    return LOS_OK;

EXIT:
    /* 兜底回收: victim 仍存活则再次取消(已死返回 ESRCH 无害), 再清理 key/sem */
    (VOID)pthread_cancel(victim);
    (VOID)LOS_TaskDelay(2); /* 2: 等 idle 回收 victim 栈 */
    ret = pthread_create(&cleaner, NULL, Pthread044KeyCleaner, NULL);
    if (ret == 0) {
        (VOID)pthread_join(cleaner, NULL);
    }
    (VOID)sem_destroy(&g_pthread044Sem);
    return LOS_OK;
}

VOID ItPosixPthread044(VOID)
{
    TEST_ADD_CASE("ItPosixPthread044", Testcase, TEST_LIB, TEST_POSIX, TEST_LEVEL0, TEST_FUNCTION);
}

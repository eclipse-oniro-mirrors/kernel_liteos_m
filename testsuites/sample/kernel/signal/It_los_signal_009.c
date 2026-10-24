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

/* 用例简要描述: 【BUG_19 缺陷暴露用例】删除带未处理待决信号的任务泄漏 OsSigInfoNode 堆节点
 *
 * 【现象】    任务 B 注册 SIGUSR1/SIGUSR2 后阻塞在 LOS_SignalWait(只等 SIGUSR2);
 *            任务 A 发送 SIGUSR1(节点入 B 的 sigCB->sigInfoList, 因 B 等待集合
 *            不含 SIGUSR1 既不唤醒也不设 OS_TASK_FLAG_SIGNAL), 随后 A 删除 B:
 *            LOS_TaskDelete 对该状态放行(无 SIGNAL 标志保护),
 *            OsTaskResourceReclaim 只 LOS_MemFree(taskCB->sig) 释放 OsSigCB 本体,
 *            不遍历 sigInfoList -> 每个未处理信号的 OsSigInfoNode 永久泄漏,
 *            且节点无任何其他回收路径。
 * 【根因】    kernel/base/los_task.c:1451-1453 OsTaskResourceReclaim 仅释放 sigCB,
 *            缺少对 sigCB->sigInfoList 链上 OsSigInfoNode 的遍历释放;
 *            (los_signal.c DeleteSigInfoFromList 仅在目标任务亲自处理信号时释放)
 * 【修复方向】OsTaskResourceReclaim 在 LOS_MemFree(taskCB->sig) 前遍历
 *            sigCB->sigInfoList, 逐节点 LOS_MemFree 后再释放 sigCB 本体。
 * 【恢复方法】修复后把 bug_isolate.h 中 KERNEL_BUG_19_SIGNAL_TASKDEL_LEAK 置 1,
 *            聚合注册点(It_los_signal.c)恢复调用, 用例恢复执行作为回归看护常跑。
 * 【关联】    BUG_19 / resleak 批次问题记录 20261023_213003。 */
#include "It_los_signal.h"
#include "los_memory.h"

static volatile UINT32 g_regFlag009 = 0;

static VOID SigHandler009(INT32 sigNo)
{
    (VOID)sigNo;
}

static VOID SubTask009(VOID)
{
    sigset_t waitSet;

    /* 注册两个信号: 置位本任务 sigSetFlag, 使主任务可向本任务发送 */
    (VOID)LOS_SignalSet(SIGUSR1, SigHandler009);
    (VOID)LOS_SignalSet(SIGUSR2, SigHandler009);
    g_regFlag009 = 1;

    /* 只等 SIGUSR2: 阻塞在 g_waitSignalList(PEND), sigWaitFlag 不含 SIGUSR1 */
    waitSet = (sigset_t)LOS_SIGNAL_MASK(SIGUSR2);
    (VOID)LOS_SignalWait(&waitSet, NULL, LOS_WAIT_FOREVER);
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 auxTid = 0;
    UINT32 loop;
    UINT32 sendRet;
    UINT32 delRet;
    UINT32 memUsedBefore;
    UINT32 memUsedAfter;
    TSK_INIT_PARAM_S taskParam;

    g_regFlag009 = 0;

    /* 基线在创建子任务前取: 子任务栈/TCB 分配后又释放净值为 0, 差值只剩真实泄漏项
     * (首轮取证 delta=-4268 即测量点晚于栈分配所致, 修正后 unfixed 应为 +node 尺寸) */
    memUsedBefore = LOS_MemTotalUsedGet(m_aucSysMem0);

    (VOID)memset_s(&taskParam, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)SubTask009;
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1; /* 高于主任务: 注册+阻塞先于主任务发送 */
    taskParam.pcName = "TaskSig009";
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    ret = LOS_TaskCreate(&auxTid, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 有界轮询: 等子任务完成信号注册并阻塞在 SignalWait */
    loop = 0;
    while ((g_regFlag009 == 0) && (loop < 3000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_regFlag009, 1, g_regFlag009, EXIT);

    /* 发 SIGUSR1: 节点入 sigInfoList; B 等待集合不含它 -> 不唤醒不设标志, 节点滞留 */
    sendRet = LOS_SignalSend(auxTid, SIGUSR1);
    ICUNIT_GOTO_EQUAL(sendRet, LOS_OK, sendRet, EXIT);

    /* 删除 B: 无 SIGNAL 标志保护 -> 放行; sigCB 释放但 sigInfoList 节点泄漏 */
    delRet = LOS_TaskDelete(auxTid);
    ICUNIT_GOTO_EQUAL(delRet, LOS_OK, delRet, EXIT);

    /* 让 idle 任务完成 B 栈回收, 消除瞬时内存抖动 */
    (VOID)LOS_TaskDelay(2);

    memUsedAfter = LOS_MemTotalUsedGet(m_aucSysMem0);
    dprintf("[Signal009] memUsed before=%u after=%u delta=%d (expect 0)\n",
            memUsedBefore, memUsedAfter, (INT32)(memUsedAfter - memUsedBefore));

    /* 修复后契约: 任务删除后其信号资源(含 sigInfoList 节点)必须全部回收 */
    ICUNIT_GOTO_EQUAL(memUsedAfter, memUsedBefore, memUsedAfter, EXIT);

    return LOS_OK;

EXIT:
    (VOID)LOS_TaskDelete(auxTid);
    (VOID)LOS_TaskDelay(2);
    return LOS_OK;
}

VOID ItLosSignal009(VOID)
{
    TEST_ADD_CASE("ItLosSignal009", Testcase, TEST_LOS, TEST_SIGNAL, TEST_LEVEL0, TEST_FUNCTION);
}

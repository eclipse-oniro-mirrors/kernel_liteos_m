/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY
 * OR FIT FOR A PARTICULAR PURPOSE.
 */

#include "It_los_signal.h"
#include "los_task.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: 重复同号信号复用链表节点二次投递
 * 子任务(优先级高于主任务)注册 SIGUSR1 handler 后 LOS_TaskDelay 长挂起(30 tick,
 * 场景需要: 处于 delay 态接收两次投递, 保证两次投递均落在挂起窗口内);
 * 主任务有界轮询 g_regFlag007(子任务已注册)后背靠背连发两次同号信号:
 * 1) 第一次 LOS_SignalSend: AddSigInfoToList 无同号节点 -> 分配新节点入队,
 *    SignalSend 走跨任务分支武装 SignalEntry(OS_TASK_FLAG_SIGNAL 未置);
 * 2) 第二次 LOS_SignalSend: AddSigInfoToList 遍历命中已存在同号节点 ->
 *    findFlag 复用节点(los_signal.c:99-101, 本用例覆盖目标), SignalSend 因
 *    OS_TASK_FLAG_SIGNAL 已置仅入队不再武装(los_signal.c:215);
 * 3) 子任务 delay 到期经 SignalEntry -> SignalHandle: 同号仅 1 个待决位 +
 *    1 个链表节点 -> handler 恰执行一次(精确断言 g_handlerCnt007 == 1),
 *    SignalEntry:179 清 OS_TASK_FLAG_SIGNAL 后恢复原上下文, 子任务正常退出,
 *    Join 后断言两次发送均返回 LOS_OK 且 handler 计数为 1。
 * 同步: g_regFlag007/g_subDone007 标志 + 有界轮询(不用裸 delay 当同步);
 * 资源: 子任务 JOINABLE 可 Join 回收, sigCB 随任务回收由内核释放(006 先例)。 */
static volatile UINT32 g_regFlag007 = 0;
static volatile UINT32 g_subDone007 = 0;
static UINT32 g_handlerCnt007 = 0;
static UINT32 g_send1Ret007 = 0xFF;
static UINT32 g_send2Ret007 = 0xFF;

#define SIGNAL007_DELAY_TICKS 30

static VOID SigHandler007(INT32 sigNo)
{
    if (sigNo == SIGUSR1) {
        g_handlerCnt007++;
    }
}

static VOID SubTask007(VOID)
{
    /* 注册 handler(同时置位 sigSetFlag, 使主任务可向本任务发送该信号) */
    (VOID)LOS_SignalSet(SIGUSR1, SigHandler007);
    g_regFlag007 = 1;

    /* 场景需要: 长挂起使两次投递均落在 delay 窗口内(允许以 delay 建立接收态) */
    (VOID)LOS_TaskDelay(SIGNAL007_DELAY_TICKS);

    /* SignalEntry 恢复原上下文后到此: handler 已恰执行一次 */
    g_subDone007 = 1;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 auxTid;
    UINT32 loop;
    TSK_INIT_PARAM_S taskParam;

    g_regFlag007 = 0;
    g_subDone007 = 0;
    g_handlerCnt007 = 0;
    g_send1Ret007 = 0xFF;
    g_send2Ret007 = 0xFF;

    (VOID)memset_s(&taskParam, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)SubTask007;
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1; /* 高于主任务: 注册+delay 先于主任务发送完成 */
    taskParam.pcName = "TaskSig007";
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.uwResved = LOS_TASK_ATTR_JOINABLE;
    ret = LOS_TaskCreate(&auxTid, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 有界轮询: 等子任务完成 handler 注册(优先级保证其先阻塞于 delay) */
    loop = 0;
    while ((g_regFlag007 == 0) && (loop < 3000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_regFlag007, 1, g_regFlag007, EXIT);

    /* 背靠背连发两次同号信号(两次之间不延时, 均落在子任务 delay 窗口内):
     * 第一次分配节点+武装 SignalEntry, 第二次复用同号节点(los_signal.c:99) */
    g_send1Ret007 = LOS_SignalSend(auxTid, SIGUSR1);
    ICUNIT_GOTO_EQUAL(g_send1Ret007, LOS_OK, g_send1Ret007, EXIT);
    g_send2Ret007 = LOS_SignalSend(auxTid, SIGUSR1);
    ICUNIT_GOTO_EQUAL(g_send2Ret007, LOS_OK, g_send2Ret007, EXIT);

    /* 有界轮询: 等子任务 delay 到期 -> SignalEntry(handler 恰一次) -> 恢复执行完成 */
    loop = 0;
    while ((g_subDone007 == 0) && (loop < 3000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_subDone007, 1, g_subDone007, EXIT);

    ret = LOS_TaskJoin(auxTid, NULL);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 精确断言: 两次同号投递复用同一待决位/链表节点, handler 恰执行一次 */
    ICUNIT_GOTO_EQUAL(g_handlerCnt007, 1, g_handlerCnt007, EXIT);

    return LOS_OK;

EXIT:
    (VOID)LOS_TaskJoin(auxTid, NULL);
    return LOS_OK;
}

/**
 * @ingroup TEST_SIGNAL
 * @par TestCase_Number
 * ItLosSignal007
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test duplicate same-number signal delivery reuses the siginfo node
 * @par TestCase_Pretreatment_Condition
 * ARM board (signal context support).
 * @par TestCase_Test_Steps
 * step1: joinable subtask (higher prio) registers SIGUSR1 handler then delays 30 ticks.
 * step2: main task sends the same signal twice back-to-back (first arms SignalEntry,
 *        second reuses the existing siginfo list node, los_signal.c:99-101).
 * step3: bounded-poll subtask completion, join, assert both sends return LOS_OK and
 *        the handler executed exactly once.
 * @par TestCase_Expected_Result
 * 1.both LOS_SignalSend calls return LOS_OK.
 * 2.handler executes exactly once (one pending bit + one reused list node).
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosSignal007(VOID)
{
    TEST_ADD_CASE("ItLosSignal007", Testcase, TEST_LOS, TEST_SIGNAL, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

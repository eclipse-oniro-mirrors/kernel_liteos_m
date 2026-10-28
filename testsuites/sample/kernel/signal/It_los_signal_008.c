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
#include "los_hwi.h"
#include "los_task.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: 中断内投递信号后等待走快速路径
 * 全部信号操作在辅助子任务内完成(006 先例: 子任务退出时 taskCB->sig 随任务
 * 回收由内核释放, 避免 sigCB 常驻泄漏):
 * 1) 子任务 LOS_SignalSet(SIGUSR1, handler) 置 sigSetFlag(使发送合法);
 * 2) LOS_HwiCreate(HWI_NUM_TEST)+LOS_HwiEnable+LOS_HwiTrigger(复用 006 的 HWI
 *    基建): HWI 处理函数中 LOS_SignalSend(当前任务, SIGUSR1) —— SignalSend
 *    自发分支因 OS_INT_ACTIVE 跳过即时处理(los_signal.c:205), 仅置
 *    sigPendFlag + 入队 + taskStatus 置 OS_TASK_FLAG_SIGNAL;
 * 3) 返回任务上下文后立即 LOS_SignalWait: sigPendFlag 非零 -> 待决快速路径
 *    (los_signal.c:395-397, 不睡眠直接取走待决信号, 本用例覆盖目标),
 *    断言返回 SIGUSR1(非超时) 且 info.si_signo 一致, 并断言该快速路径不派发
 *    handler(g_handlerCnt 快照为 0);
 * 4) 恢复(无条件 EXIT): HWI 用后即删(TestHwiDelete); 自发路径遗留的
 *    OS_TASK_FLAG_SIGNAL 会导致任务自退出被拒(los_task.c:1065), 故以任务
 *    上下文再发一次同号信号走 SignalHandle(cleanStatus=TRUE) 派发并清该标志,
 *    任务可正常退出被 Join, 无全局残留。 */
static UINT32 g_hwiSendRet008 = 0xFF;
static UINT32 g_waitRet008 = 0xFF;
static UINT32 g_waitSigno008 = 0xFF;
static UINT32 g_handlerCnt008 = 0;
static UINT32 g_handlerSnap008 = 0;
static volatile UINT32 g_hwiDone008 = 0;
static volatile UINT32 g_subDone008 = 0;
static UINT32 g_subRet008 = 0xFF;

static VOID SigHandler008(INT32 sigNo)
{
    if (sigNo == SIGUSR1) {
        g_handlerCnt008++;
    }
}

static VOID HwiHandler008(VOID)
{
    /* 中断上下文向当前任务(被打断的子任务)发信号: 仅置待决标志, 不即时处理 */
    g_hwiSendRet008 = LOS_SignalSend(LOS_CurTaskIDGet(), SIGUSR1);
    g_hwiDone008 = 1;
}

static VOID SubTask008(VOID)
{
    UINT32 ret;
    sigset_t set;
    siginfo_t info;
    HwiIrqParam irqParam;
    UINT32 loop;

    /* 1 置位 sigSetFlag, 使发送合法(handler 计数用于断言快速路径不派发) */
    (VOID)LOS_SignalSet(SIGUSR1, SigHandler008);

    /* 2 HWI 中自发信号(中断上下文仅置 pendFlag, 不即时处理) */
    (VOID)memset_s(&irqParam, sizeof(HwiIrqParam), 0, sizeof(HwiIrqParam));
    irqParam.pDevId = 0;
    g_hwiDone008 = 0;
    g_hwiSendRet008 = 0xFF;
    ret = LOS_HwiCreate(HWI_NUM_TEST, 2, 0, (HWI_PROC_FUNC)HwiHandler008, &irqParam);
    if (ret != LOS_OK) {
        goto OUT;
    }
    LOS_HwiEnable(HWI_NUM_TEST);
    LOS_HwiTrigger(HWI_NUM_TEST);

#ifdef LOSCFG_TEST_HWI_TRIG_ASYNC
    /* rk3568 GIC: SPI delivery is asynchronous (~us) - busy-wait briefly so
     * the ISR runs while THIS task is current; if it fired during the later
     * TaskDelay poll the ISR's LOS_CurTaskIDGet() would target whatever task
     * got scheduled and the signal would pend on the wrong task. */
    for (volatile UINT32 spin = 0; spin < 300000; spin++) {
        if (g_hwiDone008 == 1) {
            break;
        }
    }
#endif

    /* 有界轮询: 触发即同步执行, 1000 tick 上界防御 */
    loop = 0;
    while ((g_hwiDone008 == 0) && (loop < 1000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    (VOID)TestHwiDelete(HWI_NUM_TEST);
    if (g_hwiDone008 != 1) {
        ret = LOS_NOK;
        goto OUT;
    }
    if (g_hwiSendRet008 != LOS_OK) {
        ret = g_hwiSendRet008;
        goto OUT;
    }

    /* 3 返回任务上下文立即 Wait: 待决非零 -> 快速路径(los_signal.c:395-397),
     * 不超时不睡眠, 返回 sigNo 且 info 一致; 快速路径只取走待决信号不派发 handler */
    (VOID)sigemptyset(&set);
    (VOID)sigaddset(&set, SIGUSR1);
    (VOID)memset_s(&info, sizeof(siginfo_t), 0, sizeof(siginfo_t));
    ret = LOS_SignalWait(&set, &info, 100); /* 100, timeout 仅上界, 快速路径不触及 */
    g_waitRet008 = ret;
    g_waitSigno008 = (UINT32)info.si_signo;
    g_handlerSnap008 = g_handlerCnt008;
    if (ret != SIGUSR1) {
        goto OUT;
    }
    if (info.si_signo != SIGUSR1) {
        ret = LOS_NOK;
        goto OUT;
    }
    if (g_handlerCnt008 != 0) {
        ret = LOS_NOK;
        goto OUT;
    }
    ret = LOS_OK;

OUT:
    /* 4 恢复: HWI 已删; 自发路径遗留的 OS_TASK_FLAG_SIGNAL 必须清除
     * (否则任务自退出被拒 los_task.c:1065, Join 挂死) —— 任务上下文再发一次
     * 同号信号走 SignalHandle(cleanStatus=TRUE) 派发并清标志, 状态全清 */
    (VOID)LOS_SignalSend(LOS_CurTaskIDGet(), SIGUSR1);
    g_subRet008 = ret;
    g_subDone008 = 1;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 auxTid;
    UINT32 loop;
    TSK_INIT_PARAM_S taskParam;

    g_hwiSendRet008 = 0xFF;
    g_waitRet008 = 0xFF;
    g_waitSigno008 = 0xFF;
    g_handlerCnt008 = 0;
    g_handlerSnap008 = 0;
    g_hwiDone008 = 0;
    g_subDone008 = 0;
    g_subRet008 = 0xFF;

    (VOID)memset_s(&taskParam, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)SubTask008;
    taskParam.usTaskPrio = 10;
    taskParam.pcName = "TaskSig008";
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.uwResved = LOS_TASK_ATTR_JOINABLE;
    ret = LOS_TaskCreate(&auxTid, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    loop = 0;
    while ((g_subDone008 == 0) && (loop < 3000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_EQUAL(g_subDone008, 1, g_subDone008);

    ret = LOS_TaskJoin(auxTid, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(g_subRet008, LOS_OK, g_subRet008);
    /* HWI 内发送成功(仅置待决, 未即时处理) */
    ICUNIT_ASSERT_EQUAL(g_hwiSendRet008, LOS_OK, g_hwiSendRet008);
    /* Wait 立即返回 sigNo(快速路径, 非超时错误码) 且 info.si_signo 一致 */
    ICUNIT_ASSERT_EQUAL(g_waitRet008, SIGUSR1, g_waitRet008);
    ICUNIT_ASSERT_EQUAL(g_waitSigno008, SIGUSR1, g_waitSigno008);
    /* 快速路径取走待决信号, 不派发 handler */
    ICUNIT_ASSERT_EQUAL(g_handlerSnap008, 0, g_handlerSnap008);

    return LOS_OK;
}

/**
 * @ingroup TEST_SIGNAL
 * @par TestCase_Number
 * ItLosSignal008
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test LOS_SignalWait fast path after a signal sent from interrupt context
 * @par TestCase_Pretreatment_Condition
 * ARM board (signal context support + software hwi trigger).
 * @par TestCase_Test_Steps
 * step1: joinable subtask registers SIGUSR1 handler.
 * step2: create+enable+trigger HWI; inside the handler send the signal to the current
 *        (interrupted) task: only the pending flag is set, no immediate dispatch.
 * step3: back in task context call LOS_SignalWait: pending flag non-zero hits the fast
 *        path (los_signal.c:395-397), returns SIGUSR1 without sleeping.
 * step4: delete HWI; clear the leftover OS_TASK_FLAG_SIGNAL via one task-context
 *        self-send (SignalHandle cleanStatus=TRUE) so the subtask can exit and be joined.
 * @par TestCase_Expected_Result
 * 1.send inside HWI returns LOS_OK.
 * 2.LOS_SignalWait returns SIGUSR1 immediately, info.si_signo == SIGUSR1.
 * 3.handler is NOT dispatched by the fast path (snapshot count == 0).
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosSignal008(VOID)
{
    TEST_ADD_CASE("ItLosSignal008", Testcase, TEST_LOS, TEST_SIGNAL, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

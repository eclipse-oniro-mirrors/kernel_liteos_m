/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY
 * OR FIT FOR A PARTICULAR PURPOSE.
 */

#include "It_los_signal.h"
#include "los_hwi.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_SignalMask/LOS_SignalWait 防御与分支补充覆盖(全部信号操作在辅助
 * 子任务内完成, 子任务退出时 taskCB->sig 随任务回收由内核释放, 避免 sigCB 常驻泄漏):
 * 1) F-Inv: LOS_SignalMask(how, NULL, &oldSet) → set 为 NULL →
 *    LOS_ERRNO_SIGNAL_INVALID(los_signal.c:311-313, oldSet 先回填再拒绝);
 * 2) F-Pos: LOS_SignalMask(SIG_UNBLOCK, &set, NULL) → SIG_UNBLOCK 分支;
 * 3) F-Inv: LOS_SignalMask 非法 how(0x99) → default 分支(仅告警, 返回 LOS_OK);
 * 4) F-Inv: LOS_SignalWait(&set, &info, 0) 且无待决信号 → SignalTimedWait
 *    timeout==0 拒绝 → LOS_ERRNO_SIGNAL_INVALID(los_signal.c:340-342);
 * 5) F-Inv: 在硬件中断上下文内调用 LOS_SignalWait → OS_INT_ACTIVE 拒绝 →
 *    LOS_ERRNO_SIGNAL_PEND_INTERR(los_signal.c:345-347), 经 LOS_HwiCreate +
 *    LOS_HwiEnable + LOS_HwiTrigger 以对外接口进入中断上下文, handler 内只采样
 *    返回值不阻塞; 中断在触发指令处同步打断当前子任务, sigCB 已在子任务前序
 *    操作中分配, 无新增分配。
 * 同步: g_testCount 计数 + 有界轮询; HWI 用后即删, 子任务可等待 Join 回收。 */

static UINT32 g_sigRetInHwi006 = 0;
static volatile UINT32 g_hwiDone006 = 0;
static volatile UINT32 g_subDone006 = 0;
static UINT32 g_subRet006 = 0;

static VOID HwiHandler006(VOID)
{
    sigset_t set;
    siginfo_t info;

    (VOID)sigemptyset(&set);
    (VOID)sigaddset(&set, SIGUSR1);

    /* 中断上下文调用 LOS_SignalWait → 立即返回 PEND_INTERR, 不阻塞 */
    g_sigRetInHwi006 = LOS_SignalWait(&set, &info, 10);
    g_hwiDone006 = 1;
}

static VOID SubTask006(VOID)
{
    UINT32 ret;
    sigset_t set;
    sigset_t oldSet;
    siginfo_t info;
    HwiIrqParam irqParam;
    UINT32 loop;

    /* 1 F-Inv: set==NULL → INVALID, oldSet 已回填 */
    (VOID)sigemptyset(&oldSet);
    (VOID)sigemptyset(&set);
    (VOID)sigaddset(&set, SIGUSR2);
    ret = LOS_SignalMask(SIG_BLOCK, NULL, &oldSet);
    if (ret != LOS_ERRNO_SIGNAL_INVALID) {
        goto OUT;
    }

    /* 2 F-Pos: SIG_UNBLOCK 分支 */
    ret = LOS_SignalMask(SIG_UNBLOCK, &set, NULL);
    if (ret != LOS_OK) {
        goto OUT;
    }

    /* 2b F-Pos: SIG_BLOCK/SIG_SETMASK 回归契约 */
    ret = LOS_SignalMask(SIG_BLOCK, &set, NULL);
    if (ret != LOS_OK) {
        goto OUT;
    }
    ret = LOS_SignalMask(SIG_SETMASK, &set, &oldSet);
    if (ret != LOS_OK) {
        goto OUT;
    }
    /* 恢复: 清空掩码, 不影响后续用例 */
    (VOID)sigemptyset(&set);
    ret = LOS_SignalMask(SIG_SETMASK, &set, NULL);
    if (ret != LOS_OK) {
        goto OUT;
    }

    /* 3 F-Inv: 非法 how → default 分支(告警 + LOS_OK) */
    ret = LOS_SignalMask(0x99, &set, NULL);
    if (ret != LOS_OK) {
        goto OUT;
    }

    /* 4 F-Inv: 无待决信号 + timeout=0 → SignalTimedWait 拒绝 */
    (VOID)sigemptyset(&set);
    (VOID)sigaddset(&set, SIGUSR1);
    ret = LOS_SignalWait(&set, &info, 0);
    if (ret != LOS_ERRNO_SIGNAL_INVALID) {
        goto OUT;
    }

    /* 5 F-Inv: 中断上下文 → PEND_INTERR */
    (VOID)memset_s(&irqParam, sizeof(HwiIrqParam), 0, sizeof(HwiIrqParam));
    irqParam.pDevId = 0;
    g_hwiDone006 = 0;
    g_sigRetInHwi006 = 0;
    ret = LOS_HwiCreate(HWI_NUM_TEST, 2, 0, (HWI_PROC_FUNC)HwiHandler006, &irqParam);
    if (ret != LOS_OK) {
        goto OUT;
    }
    LOS_HwiEnable(HWI_NUM_TEST);
    LOS_HwiTrigger(HWI_NUM_TEST);

    /* 有界轮询: 触发即同步执行, 1000 tick 上界防御 */
    loop = 0;
    while ((g_hwiDone006 == 0) && (loop < 1000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    (VOID)TestHwiDelete(HWI_NUM_TEST);
    if (g_hwiDone006 != 1) {
        ret = LOS_NOK;
        goto OUT;
    }
    if (g_sigRetInHwi006 != LOS_ERRNO_SIGNAL_PEND_INTERR) {
        ret = LOS_NOK;
        goto OUT;
    }
    ret = LOS_OK;

OUT:
    g_subRet006 = ret;
    g_subDone006 = 1;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 auxTid;
    UINT32 loop;
    TSK_INIT_PARAM_S taskParam;

    g_subDone006 = 0;
    g_subRet006 = 0xFF;

    (VOID)memset_s(&taskParam, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)SubTask006;
    taskParam.usTaskPrio = 10;
    taskParam.pcName = "TaskTC006";
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.uwResved = LOS_TASK_ATTR_JOINABLE;
    ret = LOS_TaskCreate(&auxTid, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    loop = 0;
    while ((g_subDone006 == 0) && (loop < 3000)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_EQUAL(g_subDone006, 1, g_subDone006);

    ret = LOS_TaskJoin(auxTid, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(g_subRet006, LOS_OK, g_subRet006);
    ICUNIT_ASSERT_EQUAL(g_sigRetInHwi006, LOS_ERRNO_SIGNAL_PEND_INTERR, g_sigRetInHwi006);

    return LOS_OK;
}

/**
 * @ingroup TEST_SIGNAL
 * @par TestCase_Number
 * ItLosSignal006
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_SignalMask/LOS_SignalWait defensive branches
 * @par TestCase_Pretreatment_Condition
 * ARM board (signal context support).
 * @par TestCase_Test_Steps
 * step1: in a joinable subtask, run SignalMask NULL-set/UNBLOCK/illegal-how/SETMASK-restore.
 * step2: SignalWait with no pending signal and timeout 0.
 * step3: create+enable+trigger HWI, call SignalWait inside handler, bounded wait, delete HWI.
 * @par TestCase_Expected_Result
 * 1.NULL set rejected with LOS_ERRNO_SIGNAL_INVALID.
 * 2.timeout 0 rejected with LOS_ERRNO_SIGNAL_INVALID.
 * 3.Interrupt context rejected with LOS_ERRNO_SIGNAL_PEND_INTERR.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosSignal006(VOID)
{
    TEST_ADD_CASE("ItLosSignal006", Testcase, TEST_LOS, TEST_SIGNAL, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

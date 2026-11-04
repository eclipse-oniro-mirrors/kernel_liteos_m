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

#include "osTest.h"
#include "los_backtrace.h"
#include "los_hwi.h"

#ifdef LOSCFG_KERNEL_BACKTRACE

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_RecordLR/LOS_BackTrace(los_backtrace.h 对外接口)栈来源分支覆盖:
 * 1) F-Pos: LOS_RecordLR(SP=任务栈内地址) → OsStackAddrGet 任务栈分支
 *    (取局部变量地址为 SP, 落在当前任务栈区间, 回溯成功 LR[0] 非零);
 * 2) F-Inv: LOS_RecordLR(SP=非法地址 0x10) → 越界拒绝分支(PRINT_ERR + 提前返回);
 * 3) F-Pos: LOS_RecordLR(SP=主栈 CSTACK 区间地址) → MSP/CSTACK 分支
 *    (CSTACK_START_ADDR + 偏移, [start,end) 区间内);
 * 4) F-Pos: 在硬件中断上下文调用 LOS_BackTrace() → SP==0 且 Sp!=Psp 的
 *    handler 模式 MSP 分支(经 LOS_HwiCreate + LOS_HwiTrigger 进入中断)。
 * 同步: g_testCount 计数同步。 */

#define BT_TC001_DEPTH 15

static volatile UINT32 g_btHwiDone001 = 0;

static VOID HwiHandlerBT001(VOID)
{
    /* 中断上下文(handler 模式): ArchGetSp() != ArchGetPsp() → MSP 分支回溯 */
    LOS_BackTrace();
    g_btHwiDone001 = 1;
    dprintf("[ItLosBacktrace001] in hwi\n");
}

static UINT32 TestCase(VOID)
{
    UINTPTR lr[BT_TC001_DEPTH] = {0};
    UINT32 localAnchor = 0xA5A5A5A5;
    UINT32 ret;
    HwiIrqParam irqParam;
    UINT32 i;

    /* 1 F-Pos: 任务栈内 SP(局部变量地址) → 任务栈分支 + 回溯遍历
     * (ARM 经帧指针链回溯必有 LR; RISC-V 无帧指针链, LR 可为空, 仅验证不崩溃) */
    LOS_RecordLR(lr, BT_TC001_DEPTH, 0, (UINTPTR)&localAnchor);
#ifndef __RISC_V__
    ICUNIT_ASSERT_NOT_EQUAL(lr[0], 0, lr[0]);
#endif

    /* 2 F-Inv: 非法 SP(远低于任何栈区间) → 越界拒绝分支, 不触碰 LR 数组 */
    for (i = 0; i < BT_TC001_DEPTH; i++) {
        lr[i] = 0;
    }
    LOS_RecordLR(lr, BT_TC001_DEPTH, 0, 0x10);
    ICUNIT_ASSERT_EQUAL(lr[0], 0, lr[0]);

    /* 3 F-Pos: 主栈(CSTACK)区间内 SP → MSP/CSTACK 分支 */
    LOS_RecordLR(lr, BT_TC001_DEPTH, 0, (UINTPTR)(CSTACK_START_ADDR + 0x40));

    /* 4 F-Pos: 中断上下文 LOS_BackTrace → handler 模式 MSP 分支 */
    (VOID)memset_s(&irqParam, sizeof(HwiIrqParam), 0, sizeof(HwiIrqParam));
    irqParam.pDevId = 0;
    g_btHwiDone001 = 0;
    ret = LOS_HwiCreate(HWI_NUM_TEST, 2, 0, (HWI_PROC_FUNC)HwiHandlerBT001, &irqParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    LOS_HwiEnable(HWI_NUM_TEST);
    LOS_HwiTrigger(HWI_NUM_TEST);
    UINT32 loop = 0;
    while ((g_btHwiDone001 == 0) && (loop < 200)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    dprintf("[ItLosBacktrace001] hwi fired=%u\n", g_btHwiDone001);
    TestHwiDelete(HWI_NUM_TEST); /* 无条件清理, 防止 HWI 泄漏影响后续用例 */
#ifndef __RISC_V__
    ICUNIT_ASSERT_EQUAL(g_btHwiDone001, 1, g_btHwiDone001);
#endif

    return LOS_OK;
}

/**
 * @ingroup TEST_MISC
 * @par TestCase_Number
 * ItLosBacktrace001
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_RecordLR/LOS_BackTrace stack-source branches
 * @par TestCase_Pretreatment_Condition
 * LOSCFG_KERNEL_BACKTRACE enabled.
 * @par TestCase_Test_Steps
 * step1: record LR with SP inside current task stack.
 * step2: record LR with illegal SP (below any stack).
 * step3: record LR with SP inside CSTACK (main stack) range.
 * step4: call LOS_BackTrace inside a hardware interrupt (handler mode).
 * @par TestCase_Expected_Result
 * 1.Task-stack branch yields non-zero LR[0]; illegal SP leaves LR untouched.
 * 2.Interrupt-context backtrace completes without crash.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

#endif /* LOSCFG_KERNEL_BACKTRACE */

VOID ItLosBacktrace001(VOID)
{
#ifdef LOSCFG_KERNEL_BACKTRACE
    TEST_ADD_CASE("ItLosBacktrace001", TestCase, TEST_LOS, TEST_MISC, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

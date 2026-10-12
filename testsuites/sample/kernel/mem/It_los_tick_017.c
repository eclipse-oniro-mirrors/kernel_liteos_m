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

#include "It_los_mem.h"
#include "los_timer.h"
#include "los_tick.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_TickTimerRegister(los_tick.h 对外接口)参数校验分支直测。
 * 系统 tick 定时器在内核启动时已初始化(g_sysTimerIsInit=TRUE), 因此:
 * 1) F-Inv: timer=NULL 且 tickHandler=NULL → LOS_ERRNO_SYS_PTR_NULL;
 * 2) F-Inv: timer->freq=0 → LOS_ERRNO_SYS_CLOCK_INVALID(TickTimerCheck);
 * 3) F-Inv: timer->freq < LOSCFG_BASE_CORE_TICK_PER_SECOND → CLOCK_INVALID;
 * 4) F-Inv: timer->irqNum > LOSCFG_PLATFORM_HWI_LIMIT → LOS_ERRNO_TICK_CFG_INVALID;
 * 5) F-Inv: timer->periodMax=0 → TICK_CFG_INVALID;
 * 6) F-Inv: timer->init/reload/lock/unlock/getCycle 任一为 NULL → LOS_ERRNO_SYS_HOOK_IS_NULL;
 * 7) F-Inv: 结构合法的 timer → g_sysTimerIsInit → LOS_ERRNO_SYS_TIMER_IS_RUNNING
 *    (在 TickTimerCheck 内被拒, 不会替换系统 tick 定时器, 安全);
 * 8) F-Inv: timer=NULL 且 tickHandler!=NULL → 同样因已初始化返回 IS_RUNNING,
 *    不会修改系统 tickHandler, 安全。
 * 所有分支均在修改 g_sysTickTimer 之前返回, 不影响系统 tick 运行。
 * 覆盖目标: los_tick.c LOS_TickTimerRegister + TickTimerCheck 校验分支。 */

static UINT32 DummyInitTC017(HWI_PROC_FUNC tickHandler)
{
    (VOID)tickHandler;
    return LOS_OK;
}

static UINT64 DummyGetCycleTC017(UINT32 *period)
{
    if (period != NULL) {
        *period = 0;
    }
    return 0;
}

static UINT64 DummyReloadTC017(UINT64 time)
{
    return time;
}

static VOID DummyLockTC017(VOID)
{
    return;
}

static VOID DummyUnlockTC017(VOID)
{
    return;
}

static VOID DummyTickHandlerTC017(VOID)
{
    return;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    ArchTickTimer timer;

    /* 1 F-Inv: 双 NULL 入参 */
    ret = LOS_TickTimerRegister(NULL, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_PTR_NULL, ret);

    /* 构造结构完整的基准 timer, 逐项破坏一个字段验证校验分支 */
    (VOID)memset_s(&timer, sizeof(ArchTickTimer), 0, sizeof(ArchTickTimer));
    timer.freq = LOSCFG_BASE_CORE_TICK_PER_SECOND * 10; /* 合法: > tick/s */
    timer.irqNum = 1;                                   /* 合法 irq */
    timer.periodMax = 0xFFFFFFFF;
    timer.init = DummyInitTC017;
    timer.getCycle = DummyGetCycleTC017;
    timer.reload = DummyReloadTC017;
    timer.lock = DummyLockTC017;
    timer.unlock = DummyUnlockTC017;
    timer.tickHandler = (HWI_PROC_FUNC)DummyTickHandlerTC017;

    /* 2 F-Inv: freq=0 */
    timer.freq = 0;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_CLOCK_INVALID, ret);

    /* 3 F-Inv: freq < tick/s */
    timer.freq = LOSCFG_BASE_CORE_TICK_PER_SECOND / 2; /* qemu/ws63 tick/s >= 2, 结果非 0 */
    if (timer.freq == 0) {
        timer.freq = 0; /* 若 tick/s == 1 则 freq/2 == 0 已由分支 2 覆盖, 保持 0 */
    }
    if (timer.freq != 0) {
        ret = LOS_TickTimerRegister(&timer, NULL);
        ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_CLOCK_INVALID, ret);
    }

    /* 4 F-Inv: irqNum 越界 */
    timer.freq = LOSCFG_BASE_CORE_TICK_PER_SECOND * 10;
    timer.irqNum = (INT32)LOSCFG_PLATFORM_HWI_LIMIT + 1;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TICK_CFG_INVALID, ret);

    /* 5 F-Inv: periodMax=0 */
    timer.irqNum = 1;
    timer.periodMax = 0;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TICK_CFG_INVALID, ret);

    /* 6 F-Inv: 钩子函数缺失(逐个置 NULL, 每次一个) */
    timer.periodMax = 0xFFFFFFFF;
    timer.init = NULL;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_HOOK_IS_NULL, ret);

    timer.init = DummyInitTC017;
    timer.reload = NULL;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_HOOK_IS_NULL, ret);

    timer.reload = DummyReloadTC017;
    timer.lock = NULL;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_HOOK_IS_NULL, ret);

    timer.lock = DummyLockTC017;
    timer.unlock = NULL;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_HOOK_IS_NULL, ret);

    timer.unlock = DummyUnlockTC017;
    timer.getCycle = NULL;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_HOOK_IS_NULL, ret);

    /* 7 F-Pos: 结构合法的 timer → 系统已初始化 → IS_RUNNING(不会替换系统 tick) */
    timer.getCycle = DummyGetCycleTC017;
    ret = LOS_TickTimerRegister(&timer, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_TIMER_IS_RUNNING, ret);

    /* 8 F-Pos: timer=NULL + handler → 已初始化 → IS_RUNNING(不会改写系统 tickHandler) */
    ret = LOS_TickTimerRegister(NULL, (HWI_PROC_FUNC)DummyTickHandlerTC017);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_TIMER_IS_RUNNING, ret);

    return LOS_OK;
}

/**
 * @ingroup TEST_MEM
 * @par TestCase_Number
 * ItLosTick017
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_TickTimerRegister
 * @par TestCase_Pretreatment_Condition
 * system tick timer already initialized at boot.
 * @par TestCase_Test_Steps
 * step1: register with NULL timer and NULL handler.
 * step2: register crafted invalid timers (freq=0, freq<tick/s, irq overflow, periodMax=0, NULL hooks).
 * step3: register a structurally valid timer and a NULL-timer with handler.
 * @par TestCase_Expected_Result
 * 1.All invalid inputs rejected with corresponding error codes before touching the system timer.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosTick017(VOID)
{
    TEST_ADD_CASE("ItLosTick017", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

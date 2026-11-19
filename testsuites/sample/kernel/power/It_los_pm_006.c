/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd.
 *
 * Redistribution and use in source and binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * Neither the name of Huawei nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 */

/* 用例简要描述: 周期 tick 板 PM tickless 停表链契约——LOS_PmSuspend 后
 * timerStart 计数 >= 1 且编程周期在一个 tick 以上(dummy 节点回调模拟, CPU 不真正休眠) */

#include "osTest.h"
#include "It_los_pm.h"
#include "los_pm.h"
#include "los_sched.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

#define PM006_TIMER_FREQ 1000000 /* 1MHz: 100 tick(1s@100Hz) = 1e6 cycles */
#define PM006_AUX_TICKS  100     /* 辅助任务自挂 tick 数, 界定最近到期上界 */
#define PM006_MIN_CYCLES (PM006_TIMER_FREQ / LOSCFG_BASE_CORE_TICK_PER_SECOND) /* 1 tick */
#define PM006_MAX_CYCLES \
    ((UINT64)PM006_AUX_TICKS * PM006_TIMER_FREQ / LOSCFG_BASE_CORE_TICK_PER_SECOND)

static UINT32 g_auxTid006 = 0; /* 记录辅助任务 ID 供失败清理 */
static volatile UINT32 g_auxDone006 = 0;

static VOID PmAuxDelay006(VOID)
{
    (VOID)LOS_TaskDelay(PM006_AUX_TICKS); /* 界定最近到期对象: 全程 <= 100 tick */
    g_auxDone006 = 1;
}

static UINT32 g_lightCnt006 = 0;

static UINT32 PmEarly006(UINT32 mode)
{
    (VOID)mode;
    return LOS_OK;
}

static VOID PmLate006(UINT32 mode)
{
    (VOID)mode;
    return;
}

static VOID PmSuspendCheck006(UINT32 mode)
{
    (VOID)mode;
    return;
}

static UINT32 PmNormalSuspend006(VOID)
{
    return LOS_OK;
}

static VOID PmNormalResume006(VOID)
{
    return;
}

static UINT32 PmLightSuspend006(VOID)
{
    g_lightCnt006++;
    return LOS_OK;
}

static VOID PmLightResume006(VOID)
{
    return;
}

static LosPmSysctrl g_sysctrl006 = {
    .early = PmEarly006,
    .late = PmLate006,
    .suspendCheck = PmSuspendCheck006,
    .normalSuspend = PmNormalSuspend006,
    .normalResume = PmNormalResume006,
    .lightSuspend = PmLightSuspend006,
    .lightResume = PmLightResume006,
};

static UINT32 PmDeviceSuspend006(UINT32 mode)
{
    (VOID)mode;
    return LOS_OK;
}

static VOID PmDeviceResume006(UINT32 mode)
{
    (VOID)mode;
    return;
}

static LosPmDevice g_device006 = {
    .suspend = PmDeviceSuspend006,
    .resume = PmDeviceResume006,
};

static UINT32 g_tickLockCnt006 = 0;
static UINT32 g_tickUnlockCnt006 = 0;
static UINT32 g_timerStartCnt006 = 0;
static UINT32 g_timerStopCnt006 = 0;
static UINT64 g_period006 = 0; /* timerStart 实际编程周期(修复后契约的判定输入) */

static VOID PmTickLock006(VOID)
{
    g_tickLockCnt006++;
    return;
}

static VOID PmTickUnlock006(VOID)
{
    g_tickUnlockCnt006++;
    return;
}

static VOID PmTimerStart006(UINT64 period)
{
    g_timerStartCnt006++;
    g_period006 = period;
    return;
}

static VOID PmTimerStop006(VOID)
{
    g_timerStopCnt006++;
    return;
}

static UINT64 PmTimerCycleGet006(VOID)
{
    return 1000; /* 睡眠周期读数(恢复链 OsPmTickTimerStart 用), 与 005 同构 */
}

static LosPmTickTimer g_tickTimer006 = {
    .freq = PM006_TIMER_FREQ,
    .timerStart = PmTimerStart006,
    .timerStop = PmTimerStop006,
    .timerCycleGet = PmTimerCycleGet006,
    .tickLock = PmTickLock006,
    .tickUnlock = PmTickUnlock006,
};

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 suspendLoop;
    LosPmSysctrl *oldSysctrl = NULL;
    LosPmDevice *oldDevice = NULL;
    LosPmTickTimer *oldTimer = NULL;
    TSK_INIT_PARAM_S auxParam;

    g_tickLockCnt006 = 0;
    g_tickUnlockCnt006 = 0;
    g_timerStartCnt006 = 0;
    g_timerStopCnt006 = 0;
    g_period006 = 0;
    g_auxDone006 = 0;
    g_auxTid006 = 0;

    /* 前置: device + 全字段 sysctrl + LIGHT 模式(与 005 同构, dummy 不真睡) */
    ret = LOS_PmRegister(LOS_PM_TYPE_DEVICE, &g_device006);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    oldDevice = &g_device006;
    ret = LOS_PmRegister(LOS_PM_TYPE_SYSCTRL, &g_sysctrl006);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    oldSysctrl = &g_sysctrl006;
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 前置: 高优先级辅助任务自挂 100 tick, 保证最近到期对象存在且有界
     * (空链时 next-expire 为无效值, 会使 realSleepTime 回绕) */
    (VOID)memset_s(&auxParam, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    auxParam.pfnTaskEntry = (TSK_ENTRY_FUNC)PmAuxDelay006;
    auxParam.usTaskPrio = 10;
    auxParam.pcName = "PmAux006";
    auxParam.uwStackSize = TASK_STACK_SIZE_TEST;
#ifdef LOSCFG_KERNEL_SMP
    auxParam.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif
    ret = LOS_TaskCreate(&g_auxTid006, &auxParam);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 前置: 注册 PM tick timer(1MHz), timerStart 回调记录实际编程周期 */
    ret = LOS_PmRegister(LOS_PM_TYPE_TICK_TIMER, &g_tickTimer006);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    oldTimer = &g_tickTimer006;

    /* 触发: 连续 3 次 suspend(µs 级间隔, 辅助任务全程挂起, 最近到期有界);
     * 修复前(BUG_12)周期 tick 板恒走 sleepCycle==0 FALSE 分支, 停表链不执行 */
    for (suspendLoop = 0; suspendLoop < 3; suspendLoop++) {
        ret = LOS_PmSuspend(1);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    }

    /* 契约(修复后): 停表链至少执行一次且编程周期在 [1 tick, 100 tick] 内
     * —— 修复前 sleepCycle 恒 0(timerStart 计数 0)或垃圾短周期(< 1 tick),
     * 以下断言红灯即为缺陷暴露 */
    ICUNIT_GOTO_NOT_EQUAL(g_timerStartCnt006, 0, g_timerStartCnt006, EXIT);
    ICUNIT_GOTO_EQUAL(g_period006 >= PM006_MIN_CYCLES, TRUE, g_period006, EXIT);
    ICUNIT_GOTO_EQUAL(g_period006 <= PM006_MAX_CYCLES, TRUE, g_period006, EXIT);

    /* 后置: 辅助任务有界等待自然退出(成功路径同样必须等待, 否则其 8KB 栈以
     * "泄漏"形态泄入后续用例的内存窗口; 延时态删除被问题13禁止) */
    if (g_auxTid006 != 0) {
        suspendLoop = 0;
        while ((g_auxDone006 == 0) && (suspendLoop < PM006_AUX_TICKS * 4)) {
            suspendLoop++;
            (VOID)LOS_TaskDelay(1);
        }
        g_auxTid006 = 0;
    }

    return LOS_OK;

EXIT:
    /* 失败清理: 恢复默认状态, 不泄漏注册节点; 辅助任务有界等待自然退出 */
    (VOID)LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    if (oldTimer != NULL) {
        (VOID)LOS_PmUnregister(LOS_PM_TYPE_TICK_TIMER, oldTimer);
    }
    if (g_auxTid006 != 0) {
        suspendLoop = 0;
        while ((g_auxDone006 == 0) && (suspendLoop < PM006_AUX_TICKS * 4)) {
            suspendLoop++;
            (VOID)LOS_TaskDelay(1);
        }
        g_auxTid006 = 0;
    }
    if (oldSysctrl != NULL) {
        (VOID)LOS_PmUnregister(LOS_PM_TYPE_SYSCTRL, oldSysctrl);
    }
    if (oldDevice != NULL) {
        (VOID)LOS_PmUnregister(LOS_PM_TYPE_DEVICE, oldDevice);
    }
    return LOS_NOK;
}

/**
 * @ingroup TEST_FN_KERNELEXT_POWER
 * @par TestCase_Number
 * ItLosPm006
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test PM tickless stop-chain contract: LOS_PmRegister/LOS_PmSuspend/LOS_PmUnregister
 * @par TestCase_Pretreatment_Condition
 * LOSCFG_KERNEL_PM enabled.
 * @par TestCase_Test_Steps
 * step1: register dummy device/sysctrl/tick-timer and set LIGHT mode with a bounded pending timeout.
 * step2: call LOS_PmSuspend 3 times.
 * step3: assert tickless stop chain executed (timerStart count >= 1) and programmed period in [1, 100] ticks.
 * @par TestCase_Expected_Result
 * 1.PM tickless stop chain (tickLock/timerStart) executes with sane programmed period.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * CPU never really sleeps: all sysctrl callbacks are test dummies. BUG_12 regression guard.
 */

VOID ItLosPm006(VOID)
{
#if (LOSCFG_KERNEL_PM == 1)
    TEST_ADD_CASE("ItLosPm006", TestCase, TEST_LOS, TEST_FN_KERNELEXT_POWER, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

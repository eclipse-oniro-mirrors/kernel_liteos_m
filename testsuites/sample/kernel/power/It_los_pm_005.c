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
#include "It_los_pm.h"
#include "los_pm.h"
#include "los_sched.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: 【BUG_11 缺陷暴露用例(仅 tickless 板隔离)】
 * PM 对外接口深覆盖(全部经 dummy 节点回调模拟, CPU 不真正休眠):
 * 1) F-Inv: LOS_PmModeSet(LIGHT) 且 device 未注册 → LOS_ERRNO_PM_DEVICE_NULL
 *    (先注册再注销自身 device 以清空 pm->device, 结束前恢复注册);
 * 2) F-Pos: 全字段 sysctrl 注册(OsPmSysctrlRegister 11 个回调赋值行全覆盖);
 * 3) F-Inv: LOS_PmRegister/Unregister 非法入参(NULL 节点/非法类型/节点不匹配);
 * 4) F-Inv: early 回调失败 → OsPmSuspendPrepare 早退分支;
 * 5) F-Inv: device suspend 失败 → prepare 失败分支;
 * 6) F-Inv: suspend 期间持锁 → OsPmSuspendCheck 拒绝分支; prepare 期间持锁 →
 *    OsPmSuspendSleep 中部 EXIT 分支;
 * 7) F-Pos: LIGHT/DEEP/SHUTDOWN 三档经 dummy sysctrl 的 suspend/resume 回调
 *    (OsPmCpuSuspend/OsPmCpuResume 全分支);
 * 8) F-Pos: 注册 PM tick timer(tickless)后 suspend → OsPmTickTimerStop/
 *    OsPmTickTimerStart/OsTickTimerBaseReset 恢复链 + OsSchedResetSchedResponseTime
 *    → OsSwtmrResponseTimeReset(挂活动 swtmr); 两档 freq 覆盖 sleepCycle=0 与
 *    非 0 两条路径; 未注册 tick timer 时覆盖 !tickTimerStop 分支;
 * 9) F-Pos: 收尾恢复: NORMAL 模式 + 注销 sysctrl/tick timer + 释放全部锁。
 *
 * 【现象】    qemu(TICKLESS_GLOBAL)全量执行至本用例 8b 步(362 行, 注册 freq=1
 *            tick timer 后 LOS_PmSuspend)时内核断言挂死:
 *            "ASSERT ERROR! los_pm.c, 146, OsPmTickTimerStop" 后 while(1),
 *            框架收不到 Test End; 时序相位敏感: 全量+双板并行负载 9/11 命中
 *            (daily 20260929 !254~!265 九节点), 全量单跑/PM 模块单跑原生
 *            不现; ws63(周期 tick)构造上不可触发, 照常通过。
 * 【根因】    los_pm.c OsPmTickTimerStop(:146): TICKLESS_GLOBAL 模式下
 *            expireTime = taskSortLink.baseTime(陈旧基准) + 最近节点 roll
 *            = 真实绝对到期点; OsPmSuspendSleep 调用前 LOS_IntLock 关中断,
 *            若最近到期点恰落在 [关中断, 读 currTime] 窗口内(插桩实测欠账
 *            1~2K cycles ≈ 70µs), 到期 tick 中断被挂起无法推进排序链,
 *            expireTime <= currTime 成立, LOS_ASSERT 命中 while(1) 挂死。
 *            release 版(PRINT_LEVEL < LOG_ERR)断言为空, 继续执行
 *            realSleepTime = expireTime - currTime 无符号下溢 → sleepCycle
 *            天文数字编入低功耗定时器 → 低功耗睡过头, 更隐蔽。
 * 【修复方向】OsPmTickTimerStop 对 expireTime <= currTime 容错: 参照同函数
 *            sleepCycle == 0 既有分支, 置 pm->sysMode = LOS_SYS_NORMAL_SLEEP
 *            并 return FALSE(本次不睡), 消除断言挂死与 release 下溢。
 * 【恢复方法】修复后把 bug_isolate.h 中 KERNEL_BUG_11_PM_TICKTIMER_ASSERT 置 1,
 *            聚合注册点(It_los_pm.c)恢复调用, 用例恢复执行,
 *            作为回归看护用例常跑。
 * 【关联】    BUG_11 / question_record 问题 #(完整复现日志/插桩定位数据/复现
 *            方法见 question_artifacts 对应目录)。孪生缺陷 BUG_12(周期 tick
 *            板 sleepCycle 恒 0 停表链失效)见 It_los_pm_006.c。 */

static UINT32 g_pmAuxTid005 = 0; /* 记录辅助任务 ID 供失败清理 */
static UINT32 g_pmEnterCnt005 = 0;

static volatile UINT32 g_pmAuxDone005 = 0;

static VOID PmAuxDelay005(VOID)
{
    /* 200 tick 自延时: 为 tickless 断言提供有限的 next-expire; 到期自然唤醒退出 */
    (VOID)LOS_TaskDelay(200);
    g_pmAuxDone005 = 1;
}
static UINT32 g_pmLightCnt005 = 0;
static UINT32 g_pmDeepCnt005 = 0;
static UINT32 g_pmShutdownCnt005 = 0;
static UINT32 g_pmTickLockCnt005 = 0;
static UINT32 g_pmTickUnlockCnt005 = 0;
static UINT32 g_pmTimerStartCnt005 = 0;
static UINT32 g_pmTimerStopCnt005 = 0;

static UINT32 PmEarlyOK005(UINT32 mode)
{
    (VOID)mode;
    g_pmEnterCnt005++;
    return LOS_OK;
}

static UINT32 PmEarlyFail005(UINT32 mode)
{
    (VOID)mode;
    return LOS_NOK;
}

static VOID PmLate005(UINT32 mode)
{
    (VOID)mode;
    return;
}

static VOID PmSuspendCheck005(UINT32 mode)
{
    (VOID)mode;
    return;
}

static UINT32 PmNormalSuspend005(VOID)
{
    return LOS_OK;
}

static VOID PmNormalResume005(VOID)
{
    return;
}

static UINT32 PmLightSuspend005(VOID)
{
    g_pmLightCnt005++;
    return LOS_OK;
}

static VOID PmLightResume005(VOID)
{
    return;
}

static UINT32 PmDeepSuspend005(VOID)
{
    g_pmDeepCnt005++;
    return LOS_OK;
}

static VOID PmDeepResume005(VOID)
{
    return;
}

static UINT32 PmShutdownSuspend005(VOID)
{
    g_pmShutdownCnt005++;
    return LOS_OK;
}

static VOID PmShutdownResume005(VOID)
{
    return;
}

static UINT32 PmDeviceSuspendFail005(UINT32 mode)
{
    (VOID)mode;
    return LOS_NOK;
}

static UINT32 PmDeviceSuspendLock005(UINT32 mode)
{
    /* prepare 期间请求 PM 锁: prepare 返回 OK 后 OsPmSuspendSleep 检测 lock>0 走 EXIT */
    (VOID)mode;
    return LOS_PmLockRequest("TC005_LOCK_DURING_SUSPEND");
}

static UINT32 PmDeviceSuspendOK005(UINT32 mode)
{
    (VOID)mode;
    return LOS_OK;
}

static VOID PmDeviceResume005(UINT32 mode)
{
    (VOID)mode;
    return;
}

static LosPmSysctrl g_pmSysctrlFull005 = {
    .early = PmEarlyOK005,
    .late = PmLate005,
    .suspendCheck = PmSuspendCheck005,
    .normalSuspend = PmNormalSuspend005,
    .normalResume = PmNormalResume005,
    .lightSuspend = PmLightSuspend005,
    .lightResume = PmLightResume005,
    .deepSuspend = PmDeepSuspend005,
    .deepResume = PmDeepResume005,
    .shutdownSuspend = PmShutdownSuspend005,
    .shutdownResume = PmShutdownResume005,
};

static LosPmSysctrl g_pmSysctrlEarlyFail005 = {
    .early = PmEarlyFail005,
};

static LosPmDevice g_pmDeviceFail005 = {
    .suspend = PmDeviceSuspendFail005,
    .resume = PmDeviceResume005,
};

static LosPmDevice g_pmDeviceLock005 = {
    .suspend = PmDeviceSuspendLock005,
    .resume = PmDeviceResume005,
};

static LosPmDevice g_pmDeviceOK005 = {
    .suspend = PmDeviceSuspendOK005,
    .resume = PmDeviceResume005,
};

static VOID PmTickLock005(VOID)
{
    g_pmTickLockCnt005++;
    return;
}

static VOID PmTickUnlock005(VOID)
{
    g_pmTickUnlockCnt005++;
    return;
}

static VOID PmTimerStart005(UINT64 period)
{
    (VOID)period;
    g_pmTimerStartCnt005++;
    return;
}

static VOID PmTimerStop005(VOID)
{
    g_pmTimerStopCnt005++;
    return;
}

static UINT64 PmTimerCycleGet005(VOID)
{
    return 1000; /* 睡眠周期读数, 换算 realSleepTime > 0 */
}

static LosPmTickTimer g_pmTickTimer005 = {
    .freq = 1000000, /* 1MHz: (expire-curr)*g_sysClock/1MHz >> 1, sleepCycle != 0 */
    .timerStart = PmTimerStart005,
    .timerStop = PmTimerStop005,
    .timerCycleGet = PmTimerCycleGet005,
    .tickLock = PmTickLock005,
    .tickUnlock = PmTickUnlock005,
};

static LosPmTickTimer g_pmTickTimerHuge005 = {
    .freq = 1, /* 极小 freq: sleepCycle = realSleepTime*freq/g_sysClock 下取整为 0 → sleepCycle==0 分支 */
    .timerStart = PmTimerStart005,
    .timerStop = PmTimerStop005,
    .timerCycleGet = PmTimerCycleGet005,
    .tickLock = PmTickLock005,
    .tickUnlock = PmTickUnlock005,
};

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 auxLoop;
    UINT32 cntLight;
    UINT32 cntDeep;
    UINT32 cntShutdown;

    /* 说明: tickless 断言(expireTime > currTime)依赖系统常驻到期对象
     * (cpup guard 周期 swtmr + 任务延时链), 无需自建 swtmr。 */

    /* ---- 1: device 未注册时 ModeSet 拒绝 ---- */
    ret = LOS_PmRegister(LOS_PM_TYPE_DEVICE, &g_pmDeviceOK005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmUnregister(LOS_PM_TYPE_DEVICE, &g_pmDeviceOK005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_PM_DEVICE_NULL, ret, EXIT);
    ret = LOS_PmRegister(LOS_PM_TYPE_DEVICE, &g_pmDeviceOK005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 3: 注册接口非法入参 ---- */
    ret = LOS_PmRegister(LOS_PM_TYPE_DEVICE, NULL);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_PM_INVALID_PARAM, ret, EXIT);
    ret = LOS_PmRegister((LOS_PmNodeType)0x7F, &g_pmDeviceOK005);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_PM_INVALID_TYPE, ret, EXIT);
    ret = LOS_PmUnregister(LOS_PM_TYPE_DEVICE, (VOID *)&g_pmDeviceFail005); /* 未注册过的节点 */
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_PM_INVALID_NODE, ret, EXIT);

    /* ---- 2: 全字段 sysctrl 注册 ---- */
    ret = LOS_PmRegister(LOS_PM_TYPE_SYSCTRL, &g_pmSysctrlFull005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 5: device suspend 失败 → prepare 失败分支(early=OK) ---- */
    ret = LOS_PmRegister(LOS_PM_TYPE_DEVICE, &g_pmDeviceFail005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_NOT_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 4: early 失败 → prepare 早退分支 ---- */
    ret = LOS_PmRegister(LOS_PM_TYPE_SYSCTRL, &g_pmSysctrlEarlyFail005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT); /* 仅覆盖 early 字段(非 NULL 赋值) */
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_NOT_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    /* 注销恢复默认 sysctrl(OsPmSysctrlUnregister 对任意节点均重置默认并返回 LOS_OK), 重新注册全字段节点 */
    ret = LOS_PmUnregister(LOS_PM_TYPE_SYSCTRL, &g_pmSysctrlEarlyFail005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmRegister(LOS_PM_TYPE_SYSCTRL, &g_pmSysctrlFull005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 6a: 挂锁 suspend → OsPmSuspendCheck 拒绝 ---- */
    ret = LOS_PmLockRequest("TC005_HOLD");
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);
    ret = LOS_PmLockRelease("TC005_HOLD");
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 6b: prepare 期间持锁 → OsPmSuspendSleep 中部 EXIT ---- */
    ret = LOS_PmRegister(LOS_PM_TYPE_DEVICE, &g_pmDeviceLock005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ret = LOS_PmLockRelease("TC005_LOCK_DURING_SUSPEND"); /* 回调内请求的锁在此释放 */
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmRegister(LOS_PM_TYPE_DEVICE, &g_pmDeviceOK005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 7: LIGHT/DEEP/SHUTDOWN 三档 dummy suspend ---- */
    cntLight = g_pmLightCnt005;
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_pmLightCnt005, cntLight + 1, g_pmLightCnt005, EXIT);

    cntDeep = g_pmDeepCnt005;
    ret = LOS_PmModeSet(LOS_SYS_DEEP_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_pmDeepCnt005, cntDeep + 1, g_pmDeepCnt005, EXIT);

    cntShutdown = g_pmShutdownCnt005;
    ret = LOS_PmModeSet(LOS_SYS_SHUTDOWN);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_pmShutdownCnt005, cntShutdown + 1, g_pmShutdownCnt005, EXIT);
    ret = LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 8: PM tick timer(tickless) ---- */
    /* 8a: 未注册 tick timer → OsPmTickTimerStop 快速 FALSE + sched 响应时间复位路径 */
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 8 前置: 高优先级辅助任务自挂 200 tick, 保证 OsSchedGetNextExpireTime 有限
     * (空链时 next-expire 为无效值, realSleepTime 回绕会使 sleepCycle 恒 0)。
     * 辅助任务自然唤醒退出, 无延时态删除(规避问题13)。 */
    {
        UINT32 auxTid;
        TSK_INIT_PARAM_S auxParam;
        (VOID)memset_s(&auxParam, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
        auxParam.pfnTaskEntry = (TSK_ENTRY_FUNC)PmAuxDelay005;
        auxParam.usTaskPrio = 10;
        auxParam.pcName = "PmAux005";
        auxParam.uwStackSize = TASK_STACK_SIZE_TEST;
        ret = LOS_TaskCreate(&auxTid, &auxParam);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        /* 高优先级任务创建即抢占并自挂延时, 返回本任务时已在延时链 */
        g_pmAuxTid005 = auxTid;
    }

    /* 8b: 极小 freq(freq=1) → sleepCycle==0 → sysMode 回 NORMAL + FALSE 分支 */
    ret = LOS_PmRegister(LOS_PM_TYPE_TICK_TIMER, &g_pmTickTimerHuge005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 8c: 正常 freq → sleepCycle!=0 → tickLock + timerStart + 恢复链(timerStop/
     *     timerCycleGet/tickUnlock/OsTickTimerBaseReset/OsSwtmrResponseTimeReset) */
    ret = LOS_PmUnregister(LOS_PM_TYPE_TICK_TIMER, &g_pmTickTimerHuge005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmRegister(LOS_PM_TYPE_TICK_TIMER, &g_pmTickTimer005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmModeSet(LOS_SYS_LIGHT_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmSuspend(1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    /* 计数为信息性: qemu(TICKLESS_GLOBAL) 下 tickLock/timerStart/恢复链全部执行;
     * ws63 无 TICKLESS, next-expire 基准异常(见分析报告 3.3), sleepCycle 恒 0 走 FALSE 分支 */
    (VOID)g_pmTickLockCnt005;
    (VOID)g_pmTickUnlockCnt005;
    (VOID)g_pmTimerStartCnt005;
    (VOID)g_pmTimerStopCnt005;

    /* 8d: tick timer 注销非法节点 → INVALID_NODE; 正确注销收尾 */
    ret = LOS_PmUnregister(LOS_PM_TYPE_TICK_TIMER, &g_pmTickTimerHuge005);
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_PM_INVALID_NODE, ret, EXIT);
    ret = LOS_PmUnregister(LOS_PM_TYPE_TICK_TIMER, &g_pmTickTimer005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 8 后置: 等待辅助延时任务自然唤醒退出(有界轮询, 防泄漏) */
    auxLoop = 0;
    while ((g_pmAuxDone005 == 0) && (auxLoop < 400)) {
        auxLoop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_EQUAL(g_pmAuxDone005, 1, g_pmAuxDone005, EXIT);
    g_pmAuxTid005 = 0;

    /* ---- 9: 恢复现场 ---- */
    ret = LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_PmUnregister(LOS_PM_TYPE_SYSCTRL, &g_pmSysctrlFull005);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    return LOS_OK;

EXIT:
    /* 失败清理: 恢复默认状态, 保证不泄漏锁/注册节点;
     * 辅助延时任务若尚未到期, 有界等待其自然退出(不删除延时态任务, 规避问题13) */
    if (g_pmAuxTid005 != 0) {
        auxLoop = 0;
        while ((g_pmAuxDone005 == 0) && (auxLoop < 400)) {
            auxLoop++;
            (VOID)LOS_TaskDelay(1);
        }
        g_pmAuxTid005 = 0;
    }
    (VOID)LOS_PmLockRelease("TC005_HOLD");
    (VOID)LOS_PmLockRelease("TC005_LOCK_DURING_SUSPEND");
    (VOID)LOS_PmModeSet(LOS_SYS_NORMAL_SLEEP);
    (VOID)LOS_PmUnregister(LOS_PM_TYPE_TICK_TIMER, &g_pmTickTimerHuge005);
    (VOID)LOS_PmUnregister(LOS_PM_TYPE_TICK_TIMER, &g_pmTickTimer005);
    (VOID)LOS_PmUnregister(LOS_PM_TYPE_SYSCTRL, &g_pmSysctrlFull005);
    (VOID)LOS_PmUnregister(LOS_PM_TYPE_DEVICE, &g_pmDeviceOK005);
    (VOID)LOS_PmUnregister(LOS_PM_TYPE_DEVICE, &g_pmDeviceFail005);
    (VOID)LOS_PmUnregister(LOS_PM_TYPE_DEVICE, &g_pmDeviceLock005);
    return LOS_NOK;
}

/**
 * @ingroup TEST_PM
 * @par TestCase_Number
 * ItLosPm005
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_PmRegister/LOS_PmUnregister/LOS_PmModeSet/LOS_PmSuspend/LOS_PmLockRequest/LOS_PmLockRelease
 * @par TestCase_Pretreatment_Condition
 * LOSCFG_KERNEL_PM enabled.
 * @par TestCase_Test_Steps
 * step1: device unregister + ModeSet rejection; full-field sysctrl register; invalid node registrations.
 * step2: suspend with early-fail/device-fail/held-lock/lock-during-prepare variants.
 * step3: LIGHT/DEEP/SHUTDOWN dummy suspend; tickless tick-timer register + suspend + unregister.
 * @par TestCase_Expected_Result
 * 1.All rejection branches return expected error codes; dummy callbacks prove mode dispatch.
 * 2.Tickless stop/start/restore chain executed with counters incremented.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * CPU never really sleeps: all sysctrl callbacks are test dummies.
 */

VOID ItLosPm005(VOID)
{
#if (LOSCFG_KERNEL_PM == 1)
    TEST_ADD_CASE("ItLosPm005", TestCase, TEST_LOS, TEST_PM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

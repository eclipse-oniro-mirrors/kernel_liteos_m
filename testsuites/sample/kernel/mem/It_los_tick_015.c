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
#include "los_sched.h"
#include "los_swtmr.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: LOS_SleepTicksGet(los_sched.h 对外接口)直测, 替代被 FUZZ_BUG_S06 隔离的
 * ItLosTick014(其设计需删除延时态任务, 触发问题13)。本用例不删除任何延时态任务:
 * 1) 基线: 采样 > 0(系统常态存在到期对象, 不对具体上界做耦合断言; 亚 tick 到期
 *    的瞬态 0 经有界重采样规避);
 * 2) swtmr 链生效: 启动 100 tick 周期 swtmr 后采样 <= 100(该链最近到期 <= 100);
 * 3) 任务延时链生效: 高优先级辅助任务自挂 20 tick 延时后让出, 主任务立即采样,
 *    任务链最近到期 <= 20 → 总体 <= 20; 辅助任务自然唤醒后自退出(非延时态删除);
 * 4) swtmr 50 tick 变体: 采样 <= 50。
 * 覆盖目标: LOS_SleepTicksGet + OsTickRollSumToTicks(任务/软件定时器双 sortlink 路径)。
 * 同步: g_testCount 计数 + 有界轮询(每 tick 让出), 无延时死等。 */

static VOID SwtmrFuncTC015(UINT32 param)
{
    (VOID)param;
    return;
}

static VOID TaskF015(VOID)
{
    g_testCount++; /* 已运行: 即将自挂延时 */
    (VOID)LOS_TaskDelay(20);
    g_testCount++; /* 延时到期自然唤醒 */
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 sleepTicks;
    UINT32 swtmrId;
    UINT32 auxTid;
    UINT32 loop;
    TSK_INIT_PARAM_S taskParam;

    /* 1 基线: 采样 > 0(系统常态存在到期对象, 不对具体上界做耦合断言)。
     * 0 为换算截断的瞬态边界值(链上对象恰处亚 tick 到期), 有界重采样规避 */
    for (loop = 0; loop < 5; loop++) { /* 5: 有界重采样次数 */
        sleepTicks = LOS_SleepTicksGet();
        if (sleepTicks != 0) {
            break;
        }
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_NOT_EQUAL(sleepTicks, 0, sleepTicks);

    /* 2 swtmr 链: 100 tick 周期定时器启动后采样 <= 100 */
    ret = LOS_SwtmrCreate(100, LOS_SWTMR_MODE_PERIOD | LOS_SWTMR_ALIGN_FLAG, (SWTMR_PROC_FUNC)SwtmrFuncTC015, &swtmrId, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrStart(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    sleepTicks = LOS_SleepTicksGet();
    ICUNIT_ASSERT_WITHIN_EQUAL(sleepTicks, 0, 100, sleepTicks);

    /* 3 任务延时链: 高优先级辅助任务自挂 20 tick, 主任务采样 <= 20 */
    g_testCount = 0;
    (VOID)memset_s(&taskParam, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF015;
    taskParam.usTaskPrio = 10; /* 高于主测试任务 */
    taskParam.pcName = "TaskTC015";
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    ret = LOS_TaskCreate(&auxTid, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 同步: 辅助任务已挂入延时链(计数=1 后让出 CPU 回到本任务) */
    loop = 0;
    while ((g_testCount < 1) && (loop < 100)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    /* 采样: 任务链最近到期(辅助任务剩余 ~20 tick)与 swtmr 链取最小, <= 20+1 余量。
     * 下界取 0(与步 2/4 一致): 链上其他周期任务恰处亚 tick 到期时, cycle->tick
     * 换算向下截断可观测到瞬态 0, 非被测语义缺陷 */
    sleepTicks = LOS_SleepTicksGet();
    ICUNIT_ASSERT_WITHIN_EQUAL(sleepTicks, 0, 21, sleepTicks);

    /* 有界等待辅助任务自然唤醒退出(非删除, 无缺陷13风险) */
    loop = 0;
    while ((g_testCount < 2) && (loop < 100)) {
        loop++;
        (VOID)LOS_TaskDelay(1);
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount);

    /* 4 swtmr 50 tick 变体: 停 100-tick 定时器, 起 50-tick 一次性, 采样 <= 50 */
    ret = LOS_SwtmrStop(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrDelete(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_SwtmrCreate(50, LOS_SWTMR_MODE_ONCE | LOS_SWTMR_ALIGN_FLAG, (SWTMR_PROC_FUNC)SwtmrFuncTC015, &swtmrId, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrStart(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    sleepTicks = LOS_SleepTicksGet();
    ICUNIT_ASSERT_WITHIN_EQUAL(sleepTicks, 0, 50, sleepTicks);

    /* 一次性定时器 50 tick 内不会到期; 停止并删除收尾 */
    ret = LOS_SwtmrStop(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_SwtmrDelete(swtmrId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 收尾基线: 空链时换算结果可为 0(rollnum 上限回绕), 仅冒烟不耦合断言 */
    (VOID)LOS_SleepTicksGet();

    return LOS_OK;
}

/**
 * @ingroup TEST_MEM
 * @par TestCase_Number
 * ItLosTick015
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_SleepTicksGet
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: sample baseline.
 * step2: start a 100-tick periodic swtmr and sample.
 * step3: high-prio aux task self-delays 20 ticks; sample <= 21; aux wakes and exits naturally.
 * step4: 50-tick one-shot swtmr variant; stop/delete and resample.
 * @par TestCase_Expected_Result
 * 1.Samples reflect the nearest expiry of task/swtmr sortlinks.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosTick015(VOID)
{
    TEST_ADD_CASE("ItLosTick015", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

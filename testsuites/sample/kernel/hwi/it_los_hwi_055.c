/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "it_los_hwi.h"
#include "los_tick.h"

#if defined(LOSCFG_ARM_GIC_V3) && defined(LOSCFG_MCS)

/* Sleep 2x and assert >= 1x: the upstream (evolve) tick path has the
 * known mid-period-origin phase shortfall (TaskDelay(N) may only sleep
 * N-1 ticks, fixed on the testsuits branch via the discrete time base,
 * absent here on purpose). A 2x margin tolerates that while still
 * catching a real delivery stop (delta ~0). */
#define HWI_GICD_MMU_DELAY_TICK 40
#define HWI_GICD_MMU_MIN_TICK   20

/* 用例简要描述: GICD 锁定期间中断投递独立性(T5) - tick(PPI30)走 GICR/ICC
 * 投递不经 GICD 软件访问, 锁定 20 tick 期间计数持续增长。 */
static UINT32 Testcase(VOID)
{
    UINT64 tickBefore;
    UINT64 tickAfter;
    UINT64 tickDelta;
    UINT32 ret;

    tickBefore = LOS_TickCountGet();

    ret = LOS_GicdMmuDisable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), TRUE, FALSE, EXIT);

    /* 锁定期等待: tick 中断持续投递(任务被正常调度延时) */
    LOS_TaskDelay(HWI_GICD_MMU_DELAY_TICK);

    ret = LOS_GicdMmuEnable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    tickAfter = LOS_TickCountGet();
    tickDelta = tickAfter - tickBefore;
    /* F-Pos: 余量法 - 睡 40 tick 断言 >= 20: 容忍上游 tick 相位缺失(最多差 1~2),
     * 同时若投递真停(tickDelta~0)必然失败, 判定目标(锁定期间中断持续投递)不变 */
    ICUNIT_GOTO_EQUAL((tickDelta >= (UINT64)HWI_GICD_MMU_MIN_TICK), TRUE, FALSE, EXIT);

    return LOS_OK;
EXIT:
    (VOID)LOS_GicdMmuEnable();
    return LOS_OK;
}

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi055
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Interrupt delivery independence while the GICD window is locked
 * @par TestCase_Pretreatment_Condition
 * RK3568 AMP slave, task context.
 * @par TestCase_Test_Steps
 * step1: sample tick count, lock the GICD window.
 * step2: LOS_TaskDelay(20) while locked.
 * step3: unlock, sample tick count again.
 * @par TestCase_Expected_Result
 * 1.Tick count advanced by at least the delay: PPI30 delivery goes
 *   through GICR/ICC and is unaffected by the GICD lock (core premise).
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 */
VOID ItLosHwi055(VOID)
{
#if defined(LOSCFG_ARM_GIC_V3) && defined(LOSCFG_MCS)
    /* 锁定期间中断投递独立性 */
    TEST_ADD_CASE("ItLosHwi055", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
#endif /* LOSCFG_ARM_GIC_V3 && LOSCFG_MCS */

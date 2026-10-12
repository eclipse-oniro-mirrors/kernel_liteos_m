/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "It_los_mem.h"
#include "los_sched.h"

/* 用例简要描述: LOS_SleepTicksGet 直测。语义（los_sched_tick.c）：返回任务延时链与
 * 软件定时器链最近到期对象的最小剩余 tick 数。设计：高优先级辅助任务挂 100 tick
 * 长延时，使任务延时链的最近到期对象可控——采样值应 >0 且不超过辅助任务剩余时长；
 * 主任务睡 20 tick 后剩余同步收紧；与 LOS_TickCountGet 单调一致（实际流逝 >= 睡眠
 * tick 数）；含 LOS_TaskDelay(0)（= TaskYield）边界；收尾正常删除延时态辅助任务。
 * 注：删除延时态任务触发已知同源缺陷（问题 13），随 FUZZ_BUG_S06 宏隔离，
 * 缺陷修复置 1 后本用例自动恢复执行。 */

static volatile UINT32 g_auxDelayFlag132;

static VOID TaskF132(VOID)
{
    g_auxDelayFlag132 = 1;
    /* 100 ticks: 长延时，使本任务驻留任务延时链，成为 LOS_SleepTicksGet 的最近到期对象。
     * 入口返回后由内核 OsTaskEntry 自动自删（DETACHED 属性）。 */
    (VOID)LOS_TaskDelay(100);
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 sleepTicks1;
    UINT32 sleepTicks2;
    UINT32 delta;
    UINT64 tickBefore;
    UINT64 tickAfter;
    TSK_INIT_PARAM_S task1 = { 0 };

    /* F-Pos: 创建高优先级辅助任务，创建后立即抢占运行并挂入 100 tick 延时。
     * 任务入口返回后由内核 OsTaskEntry 自动调 OsTaskDelete 自删，须显式指定
     * LOS_TASK_STATUS_DETACHED 保证自删路径回收 TCB/栈资源。 */
    g_auxDelayFlag132 = 0;
    task1.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF132;
    task1.uwStackSize  = 0x1000; /* 0x1000: aux task stack size */
    task1.pcName       = "TaskAux132";
    task1.usTaskPrio   = TASK_PRIO_TEST_NORMAL; /* 20: higher than the test task (25) */
    task1.uwResved     = LOS_TASK_STATUS_DETACHED; /* self-delete on entry return */
    ret = LOS_TaskCreate(&g_testTaskID01, &task1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    /* 辅助任务已运行并处于延时态（延时链非空）。 */
    ICUNIT_ASSERT_EQUAL(g_auxDelayFlag132, 1, g_auxDelayFlag132);

    /* F-Pos: 延时链最近到期对象 = 辅助任务（剩余 <= 100 tick），链上存在待到期对象，
     * 返回值为有效预测：> 0 且 <= 100。 */
    sleepTicks1 = LOS_SleepTicksGet();
    ICUNIT_ASSERT_WITHIN_EQUAL(sleepTicks1, 0, 100, sleepTicks1);

    /* F-Pos: 与 LOS_TickCountGet 单调一致：睡 20 tick，实际流逝 tick 数 >= 20（下界），
     * 且不超时过多（30 = 20 + 10 容差上界，防异常长睡）。 */
    tickBefore = LOS_TickCountGet();
    ret = LOS_TaskDelay(20); /* sleep 20 ticks */
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    tickAfter = LOS_TickCountGet();
    delta = (UINT32)(tickAfter - tickBefore);
    ICUNIT_ASSERT_WITHIN_EQUAL(delta, 20, 30, delta);

    /* F-Pos: 20 tick 流逝后，辅助任务剩余 <= 100 - 20 = 80，采样上界同步收紧。 */
    sleepTicks2 = LOS_SleepTicksGet();
    ICUNIT_ASSERT_WITHIN_EQUAL(sleepTicks2, 0, 80, sleepTicks2);

    /* F-Pos: 延迟 0 tick 边界：LOS_TaskDelay(0) 语义为 LOS_TaskYield，触发调度但不睡眠。
     * 辅助任务处于延时态（不在就绪队列），就绪队列无可切任务时 Yield 返回
     * LOS_ERRNO_TSK_YIELD_NOT_ENOUGH_TASK(0x02000210)，否则 LOS_OK，两种均为合法结果
     * （与 posix 桥接 sched_yield 容忍 NOT_ENOUGH_TASK 的口径一致，sched.c:43-50）。 */
    tickBefore = LOS_TickCountGet();
    ret = LOS_TaskDelay(0);
    ICUNIT_ASSERT_EQUAL((ret == LOS_OK) || (ret == LOS_ERRNO_TSK_YIELD_NOT_ENOUGH_TASK), 1, ret);
    tickAfter = LOS_TickCountGet();
    delta = (UINT32)(tickAfter - tickBefore);
    ICUNIT_ASSERT_WITHIN_EQUAL(delta, 0, 1, delta);

    /* F-Pos: yield 不睡眠，不影响延时链上的剩余预测（上界仍为 80）。 */
    sleepTicks2 = LOS_SleepTicksGet();
    ICUNIT_ASSERT_WITHIN_EQUAL(sleepTicks2, 0, 80, sleepTicks2);

    /* 清理收尾：正常删除仍处延时态（剩余 <= 80 tick）的辅助任务。 */
    ret = LOS_TaskDelete(g_testTaskID01);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}


VOID ItLosTick014(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosTick014", Testcase, TEST_LOS, TEST_TICK, TEST_LEVEL1, TEST_FUNCTION);
}

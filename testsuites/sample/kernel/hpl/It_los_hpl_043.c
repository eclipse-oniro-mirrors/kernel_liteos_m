/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor any of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "It_los_hpl.h"

/* 用例简要描述: HPL 压力测试——8 任务并发高量记录 240 条守恒、10 轮任务增删 churn（每轮 6 任务×8 条=480 条）遍历句柄槽位复用与 preDelete 回收路径，总条目 720 精确守恒且零遗弃零泄漏 */

#define HPL_043_HI_TASKS       8
#define HPL_043_HI_EACH        30
#define HPL_043_CH_ROUNDS      10
#define HPL_043_CH_TASKS       6
#define HPL_043_CH_EACH        8
#define HPL_043_WAIT_CAP       400
#define HPL_043_TOTAL_CNT      (HPL_043_HI_TASKS * HPL_043_HI_EACH + \
                                HPL_043_CH_ROUNDS * HPL_043_CH_TASKS * HPL_043_CH_EACH)

STATIC volatile UINT32 g_go43;
STATIC UINT32 g_task43Ids[HPL_043_HI_TASKS];

/* High-volume recorder: released together with its peers for a real burst. */
static VOID TaskF43Hi(VOID)
{
    UINT32 i;
    while (g_go43 == 0) {
        LOS_TaskDelay(1);
    }
    for (i = 0; i < HPL_043_HI_EACH; i++) {
        HP_LOG(HPL_DEBUG, "s43 hi %u %d", i, (INT32)i);
    }
    g_testCount++;
}

/* Churn worker: logs and exits immediately (DETACHED auto-delete), so each
 * round drives the handle slot through create -> log -> preDelete -> recycle,
 * and the next round's taskId reuse exercises the in-line reclaim path in
 * OsHplGetHplHandle (hpl->inited && hpl->preDelete -> compress + recreate). */
static VOID TaskF43Ch(VOID)
{
    UINT32 i;
    for (i = 0; i < HPL_043_CH_EACH; i++) {
        HP_LOG(HPL_DEBUG, "s43 churn %u %d", i, (INT32)i);
    }
    g_testCount++;
}

static UINT32 TestCase(VOID)
{
    UINT32 i;
    UINT32 round;
    UINT32 ret;
    UINT32 before;
    UINT32 delta;
    UINT32 abandBefore;
    UINT32 wait;
    TSK_INIT_PARAM_S taskParam = {0};

    TestHplPrepare();
    before = g_hplArgsEntryCnt;
    abandBefore = g_hplAbandonedCnt;

    /* Phase A: 8 recorders released together, 30 entries each. */
    g_go43 = 0;
    g_testCount = 0;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF43Hi;
    taskParam.pcName = "HplS43Hi";
    for (i = 0; i < HPL_043_HI_TASKS; i++) {
        ret = LOS_TaskCreate(&g_task43Ids[i], &taskParam);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }
    g_go43 = 1;
    wait = 0;
    while ((g_testCount != HPL_043_HI_TASKS) && (wait < HPL_043_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, HPL_043_HI_TASKS, g_testCount);
    wait = 0;
    while ((g_hplArgsEntryCnt - before) < (HPL_043_HI_TASKS * HPL_043_HI_EACH) &&
           (wait < HPL_043_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    delta = g_hplArgsEntryCnt - before;
    ICUNIT_ASSERT_EQUAL(delta, HPL_043_HI_TASKS * HPL_043_HI_EACH, delta);

    /* Phase B: churn rounds. The runners create 6 workers per round; each
     * worker preempts (higher priority), logs 8 entries and auto-deletes, so
     * the rounds are serialized while the compression task keeps draining. */
    for (round = 0; round < HPL_043_CH_ROUNDS; round++) {
        g_testCount = 0;
        taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF43Ch;
        taskParam.pcName = "HplS43Ch";
        for (i = 0; i < HPL_043_CH_TASKS; i++) {
            ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
            ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        }
        wait = 0;
        while ((g_testCount != HPL_043_CH_TASKS) && (wait < HPL_043_WAIT_CAP)) {
            LOS_TaskDelay(1);
            wait++;
        }
        ICUNIT_ASSERT_EQUAL(g_testCount, HPL_043_CH_TASKS, g_testCount);
        LOS_TaskDelay(2); /* let the idle task recycle the deleted TCBs */
    }

    /* Final conservation over the whole stress: exactly 720 entries, zero
     * abandoned logs (staging-full always preempts into the compression
     * task at prio 2, churn reclaim compresses in line). */
    wait = 0;
    while ((g_hplArgsEntryCnt - before) < HPL_043_TOTAL_CNT && (wait < HPL_043_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    delta = g_hplArgsEntryCnt - before;
    ICUNIT_ASSERT_EQUAL(delta, HPL_043_TOTAL_CNT, delta);
    ICUNIT_ASSERT_EQUAL(g_hplAbandonedCnt - abandBefore, 0, g_hplAbandonedCnt - abandBefore);

    TestHplPrepare();
    return LOS_OK;
}

VOID ItLosHpl043(VOID)
{
    TEST_ADD_CASE("ItLosHpl043", TestCase, TEST_LOS, TEST_FN_KERNELEXT_HPL, TEST_LEVEL0, TEST_FUNCTION);
}

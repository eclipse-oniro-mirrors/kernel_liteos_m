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

/* 用例简要描述: HPL 多任务并发调用——6 任务并发记录条目守恒 300、2 任务并发交替 SetLogRecordHook/PeakSizeGet 干扰无异常返回、双任务并发对同一目标任务 TaskHplSizeSet 恰一成功（无锁 check-then-set 竞态契约）且句柄事后可用 */

#define HPL_042_RECORDER_N    6
#define HPL_042_REC_EACH      50
#define HPL_042_INF_N         2
#define HPL_042_INF_LOOPS     100
#define HPL_042_X_LOGS        3
#define HPL_042_WAIT_CAP      250

STATIC volatile UINT32 g_go42;
STATIC volatile UINT32 g_go42Set;
STATIC volatile UINT32 g_gate42X;
STATIC volatile UINT32 g_err42Hook;
STATIC volatile UINT32 g_err42Peak;
STATIC UINT32 g_set42Ret[HPL_042_INF_N];
STATIC UINT32 g_task42Ids[HPL_042_RECORDER_N];
STATIC UINT32 g_x42Id;
STATIC UINT32 g_setter42Ids[HPL_042_INF_N];

/* Recorder: bursts HP_LOG entries from its own staging buffer. */
static VOID TaskF42Rec(VOID)
{
    UINT32 i;
    while (g_go42 == 0) {
        LOS_TaskDelay(1);
    }
    for (i = 0; i < HPL_042_REC_EACH; i++) {
        HP_LOG(HPL_DEBUG, "c42 rec %u concurrent", i);
    }
    g_testCount++;
}

/* Interferer: swaps the record hook and queries its own peak while the
 * recorders are logging. Both hooks keep the shared statistics, so the entry
 * conservation of the recorders is not disturbed. */
static VOID TaskF42Inf(VOID)
{
    UINT32 i;
    UINT32 peak;
    BOOL ovf;
    UINT32 ret;
    while (g_go42 == 0) {
        LOS_TaskDelay(1);
    }
    for (i = 0; i < HPL_042_INF_LOOPS; i++) {
        ret = LOS_HplSetLogRecordHook((i % 2) == 0 ? TestHplProbeHook : TestHplRecordHook);
        if (ret != LOS_OK) {
            g_err42Hook++;
        }
        ret = LOS_TaskHplPeakSizeGet(LOS_CurTaskIDGet(), &peak, &ovf);
        /* This task never logs: NOT_CREATED is the steady state, OK can be
         * observed transiently when the taskId slot is still pending free. */
        if ((ret != LOS_OK) && (ret != LOS_ERRNO_HPL_NOT_CREATED)) {
            g_err42Peak++;
        }
        LOS_TaskDelay(1);
    }
    g_testCount++;
}

/* Concurrent setter: both instances race LOS_TaskHplSizeSet on the same
 * target task; the contract is exactly one LOS_OK and one HAS_CREATED. */
static VOID TaskF42Set(VOID)
{
    while (g_go42Set == 0) {
        LOS_TaskDelay(1);
    }
    g_set42Ret[0] = LOS_TaskHplSizeSet(g_x42Id, HPL_TEST_STAGING_SIZE);
    g_testCount++;
}

static VOID TaskF42Set2(VOID)
{
    while (g_go42Set == 0) {
        LOS_TaskDelay(1);
    }
    g_set42Ret[1] = LOS_TaskHplSizeSet(g_x42Id, HPL_TEST_STAGING_SIZE);
    g_testCount++;
}

/* Race target: parks until the setters are done, then proves the handle the
 * winner created is usable by logging through it. */
static VOID TaskF42X(VOID)
{
    UINT32 i;
    while (g_gate42X == 0) {
        LOS_TaskDelay(1);
    }
    for (i = 0; i < HPL_042_X_LOGS; i++) {
        HP_LOG(HPL_DEBUG, "c42 x target %u", i);
    }
    g_testCount++;
}

static UINT32 TestCase(VOID)
{
    UINT32 i;
    UINT32 ret;
    UINT32 before;
    UINT32 delta;
    UINT32 wait;
    TSK_INIT_PARAM_S taskParam = {0};

    TestHplPrepare();
    before = g_hplArgsEntryCnt;
    g_err42Hook = 0;
    g_err42Peak = 0;
    g_go42 = 0;
    g_testCount = 0;

    /* Phase A: 6 recorders + 2 interferers released together. */
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.usTaskPrio = TASK_PRIO_TEST - 2; /* interferers above recorders */
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF42Inf;
    taskParam.pcName = "HplC42Inf";
    for (i = 0; i < HPL_042_INF_N; i++) {
#ifdef LOSCFG_KERNEL_SMP
            taskParam.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif
        ret = LOS_TaskCreate(&g_setter42Ids[i], &taskParam);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }

    taskParam.usTaskPrio = TASK_PRIO_TEST - 1; /* recorders */
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF42Rec;
    taskParam.pcName = "HplC42Rec";
    for (i = 0; i < HPL_042_RECORDER_N; i++) {
#ifdef LOSCFG_KERNEL_SMP
            taskParam.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif
        ret = LOS_TaskCreate(&g_task42Ids[i], &taskParam);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }

    g_go42 = 1;
    wait = 0;
    while ((g_testCount != HPL_042_RECORDER_N + HPL_042_INF_N) && (wait < HPL_042_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, HPL_042_RECORDER_N + HPL_042_INF_N, g_testCount);

    /* Entry conservation across the concurrent burst + interference. */
    wait = 0;
    while ((g_hplArgsEntryCnt - before) < (HPL_042_RECORDER_N * HPL_042_REC_EACH) &&
           (wait < HPL_042_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    delta = g_hplArgsEntryCnt - before;
    ICUNIT_ASSERT_EQUAL(delta, HPL_042_RECORDER_N * HPL_042_REC_EACH, delta);
    ICUNIT_ASSERT_EQUAL(g_err42Hook, 0, g_err42Hook);
    ICUNIT_ASSERT_EQUAL(g_err42Peak, 0, g_err42Peak);

    /* Phase B: two setters race LOS_TaskHplSizeSet on the same target. The
     * contract is exactly one winner; a double LOS_OK would expose the
     * unlocked check-then-set in OsHplCreate (memory leak + double init). */
    g_go42Set = 0;
    g_gate42X = 0;
    g_testCount = 0;
    taskParam.usTaskPrio = TASK_PRIO_TEST - 2;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF42X;
    taskParam.pcName = "HplC42X";
#ifdef LOSCFG_KERNEL_SMP
    taskParam.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif
    ret = LOS_TaskCreate(&g_x42Id, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF42Set;
    taskParam.pcName = "HplC42S1";
#ifdef LOSCFG_KERNEL_SMP
        taskParam.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif
    ret = LOS_TaskCreate(&g_setter42Ids[0], &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF42Set2;
    taskParam.pcName = "HplC42S2";
#ifdef LOSCFG_KERNEL_SMP
        taskParam.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
#endif
    ret = LOS_TaskCreate(&g_setter42Ids[1], &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    g_go42Set = 1;
    wait = 0;
    while ((g_testCount != 2) && (wait < HPL_042_WAIT_CAP)) { /* 2: both setters */
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount);

    ICUNIT_ASSERT_EQUAL((g_set42Ret[0] == LOS_OK) || (g_set42Ret[1] == LOS_OK), 1,
                        (g_set42Ret[0] << 16) | g_set42Ret[1]);
    ICUNIT_ASSERT_EQUAL((g_set42Ret[0] == LOS_ERRNO_HPL_HAS_CREATED) ||
                        (g_set42Ret[1] == LOS_ERRNO_HPL_HAS_CREATED), 1,
                        (g_set42Ret[0] << 16) | g_set42Ret[1]);
    ICUNIT_ASSERT_EQUAL((g_set42Ret[0] == LOS_OK) + (g_set42Ret[1] == LOS_OK), 1,
                        (g_set42Ret[0] << 16) | g_set42Ret[1]);

    /* The winning handle is usable and queryable. */
    UINT32 peak;
    BOOL ovf;
    ret = LOS_TaskHplPeakSizeGet(g_x42Id, &peak, &ovf);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(peak, 0, peak);

    g_testCount = 0;
    g_gate42X = 1;
    wait = 0;
    while ((g_testCount != 1) && (wait < HPL_042_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    wait = 0;
    while ((g_hplArgsEntryCnt - before) <
           (HPL_042_RECORDER_N * HPL_042_REC_EACH + HPL_042_X_LOGS) && (wait < HPL_042_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    delta = g_hplArgsEntryCnt - before;
    ICUNIT_ASSERT_EQUAL(delta, HPL_042_RECORDER_N * HPL_042_REC_EACH + HPL_042_X_LOGS, delta);

    TestHplPrepare();
    return LOS_OK;
}

VOID ItLosHpl042(VOID)
{
    TEST_ADD_CASE("ItLosHpl042", TestCase, TEST_LOS, TEST_FN_KERNELEXT_HPL, TEST_LEVEL0, TEST_FUNCTION);
}

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

/* 用例简要描述: HPL 接口重复调用——SetLogLevel 交替 200 次读回一致、SetLogRecordHook 交替 200 次后日志计数正常、RecordCustomString 重复 200 次 hook 守恒、TaskHplSizeSet 重复设置错误码稳定且句柄可用、TaskHplPeakSizeGet 重复查询峰值单调稳定、HP_LOG 同站点 400 次快速重复条目守恒 */

#define HPL_041_LEVEL_LOOPS    200
#define HPL_041_HOOK_LOOPS     200
#define HPL_041_CUSTOM_LOOPS   200
#define HPL_041_BURST_CNT      400
#define HPL_041_SETLOGS        5
#define HPL_041_WAIT_CAP       200

STATIC volatile UINT32 g_gate41;
STATIC UINT32 g_peak41;

/* Gated worker: waits for the gate so the test task can register its staging
 * buffer first (deterministic SizeSet outcome), then logs a few entries and
 * waits again until the peak queries are done. */
static VOID TaskF41Set(VOID)
{
    UINT32 i;
    while (g_gate41 == 0) {
        LOS_TaskDelay(1);
    }
    for (i = 0; i < HPL_041_SETLOGS; i++) {
        HP_LOG(HPL_DEBUG, "rep41 set task %u", i);
    }
    g_testCount++;
    while (g_gate41 != 2) { /* 2: release after the peak queries */
        LOS_TaskDelay(1);
    }
}

/* Burst worker: repeated HP_LOG through one call site. */
static VOID TaskF41Log(VOID)
{
    UINT32 i;
    for (i = 0; i < HPL_041_BURST_CNT; i++) {
        HP_LOG(HPL_DEBUG, "rep41 burst %u", i);
    }
    g_testCount++;
}

/* Wait until the args-entry counter grows by want, capped by ticks. */
STATIC UINT32 TestHpl41WaitEntries(UINT32 before, UINT32 want, UINT32 capTicks, UINT32 *delta)
{
    UINT32 wait = 0;
    while ((g_hplArgsEntryCnt - before) < want) {
        if (wait >= capTicks) {
            break;
        }
        LOS_TaskDelay(1);
        wait++;
    }
    *delta = g_hplArgsEntryCnt - before;
    return ((*delta) == want) ? LOS_OK : LOS_NOK;
}

static UINT32 TestCase(VOID)
{
    UINT32 i;
    UINT32 ret;
    UINT32 level;
    UINT32 before;
    UINT32 delta;
    UINT32 peak;
    BOOL ovf;
    UINT32 wait;
    TSK_INIT_PARAM_S taskParam = {0};

    TestHplPrepare();

    /* 1. SetLogLevel repeated alternating writes: every call returns LOS_OK
     * and the value reads back consistently (no concurrent writer). */
    for (i = 0; i < HPL_041_LEVEL_LOOPS; i++) {
        level = (i % 3 == 0) ? HPL_TEST_LOG_LEVEL : ((i % 3 == 1) ? HPL_ERROR : HPL_SILENT);
        ret = LOS_HplSetLogLevel(level);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        if ((i % 50) == 49) { /* spot readback every 50 writes */
            ICUNIT_ASSERT_EQUAL(OsHplGetLogLevel(), level, OsHplGetLogLevel());
        }
    }
    ret = LOS_HplSetLogLevel(HPL_TEST_LOG_LEVEL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(OsHplGetLogLevel(), HPL_TEST_LOG_LEVEL, OsHplGetLogLevel());

    /* 2. SetLogRecordHook repeated swaps (both hooks keep the shared
     * statistics up to date, so later counting stays valid). */
    for (i = 0; i < HPL_041_HOOK_LOOPS; i++) {
        ret = LOS_HplSetLogRecordHook((i % 2) == 0 ? TestHplProbeHook : TestHplRecordHook);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }
    ret = LOS_HplSetLogRecordHook(TestHplRecordHook);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 3. RecordCustomString repeated calls: every call delivers straight to
     * the record hook (no compression involved). Settle first so residual
     * deliveries from earlier cases cannot distort the exact count. */
    LOS_TaskDelay(2); /* 2 ticks: beyond one compress-slice residue */
    before = g_hplHookCalls;
    for (i = 0; i < HPL_041_CUSTOM_LOOPS; i++) {
        ret = LOS_HplRecordCustomString("rep041", sizeof("rep041"));
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }
    ICUNIT_ASSERT_EQUAL(g_hplHookCalls - before, HPL_041_CUSTOM_LOOPS, g_hplHookCalls - before);

    /* 4. TaskHplSizeSet repeated on one live task: first succeeds, repeats
     * are rejected with a stable error code, the handle stays usable and the
     * peak queries are consistent before/after logging. */
    g_gate41 = 0;
    g_testCount = 0;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF41Set;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplRep41S";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_TaskHplSizeSet(g_testTaskID01, HPL_TEST_STAGING_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_TaskHplSizeSet(g_testTaskID01, HPL_TEST_STAGING_SIZE * 2); /* repeat: bigger */
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_HAS_CREATED, ret);
    ret = LOS_TaskHplSizeSet(g_testTaskID01, HPL_TEST_STAGING_SIZE);     /* repeat: same */
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HPL_HAS_CREATED, ret);

    for (i = 0; i < 5; i++) { /* repeated peak queries before any logging */
        ret = LOS_TaskHplPeakSizeGet(g_testTaskID01, &peak, &ovf);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        ICUNIT_ASSERT_EQUAL(peak, 0, peak);
        ICUNIT_ASSERT_EQUAL(ovf, FALSE, ovf);
    }

    before = g_hplArgsEntryCnt;
    g_gate41 = 1;
    wait = 0;
    while ((g_testCount != 1) && (wait < HPL_041_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    ret = TestHpl41WaitEntries(before, HPL_041_SETLOGS, HPL_041_WAIT_CAP, &delta);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, delta);

    g_peak41 = 0;
    for (i = 0; i < 5; i++) { /* repeated peak queries after logging: >0 and stable */
        ret = LOS_TaskHplPeakSizeGet(g_testTaskID01, &peak, &ovf);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        ICUNIT_ASSERT_EQUAL((peak > 0), 1, peak);
        ICUNIT_ASSERT_EQUAL(ovf, FALSE, ovf);
        if (g_peak41 == 0) {
            g_peak41 = peak;
        } else {
            ICUNIT_ASSERT_EQUAL(peak, g_peak41, peak); /* monotonic and stable */
        }
    }
    g_gate41 = 2; /* release the gated worker so it exits and is recycled */
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    /* 5. HP_LOG repeated through one call site: entry conservation under a
     * tight burst (the staging-full event preempts into the compression
     * task, so no entry is abandoned). */
    g_testCount = 0;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF41Log;
    taskParam.pcName = "HplRep41L";
    ret = LOS_TaskCreate(&g_testTaskID02, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    wait = 0;
    while ((g_testCount != 1) && (wait < HPL_041_WAIT_CAP)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    ret = TestHpl41WaitEntries(before, HPL_041_SETLOGS + HPL_041_BURST_CNT, HPL_041_WAIT_CAP * 2,
                               &delta);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, delta);
    ICUNIT_ASSERT_EQUAL(delta, HPL_041_SETLOGS + HPL_041_BURST_CNT, delta);

    TestHplPrepare();
    return LOS_OK;
}

VOID ItLosHpl041(VOID)
{
    TEST_ADD_CASE("ItLosHpl041", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}

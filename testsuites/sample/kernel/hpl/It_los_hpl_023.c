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
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
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

static VOID TaskF23a(VOID)
{
    UINT32 i;

    for (i = 0; i < HPL_TEST_LOG_COUNT; i++) {
        HP_LOG(HPL_DEBUG, "del %u", i);
    }
    g_testCount = 1;
}

static VOID TaskF23b(VOID)
{
    UINT32 i;

    for (i = 0; i < HPL_TEST_LOG_COUNT; i++) {
        HP_LOG(HPL_DEBUG, "reuse %u", i);
    }
    g_testCount = 2;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 wait = 0;
    UINT32 taskAId;
    UINT32 taskBId;
    UINT32 peakSize = 0;
    BOOL overflow = FALSE;
    UINT32 argsBefore;
    UINT32 argsNow;
    TSK_INIT_PARAM_S taskParam = {0};

    TestHplPrepare();
    argsBefore = g_hplArgsEntryCnt;
    g_testCount = 0;

    /* Task A logs and exits on its own (detached auto-delete). */
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF23a;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplDel23a";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    taskAId = g_testTaskID01;

    while ((g_testCount != 1) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    /* Wait until A is fully recycled: the task APIs reject its id afterwards
     * and the staging buffer has been freed by the compress task. */
    wait = 0;
    do {
        ret = LOS_TaskHplPeakSizeGet(taskAId, &peakSize, &overflow);
        if (ret == LOS_ERRNO_TSK_NOT_CREATED) {
            break;
        }
        LOS_TaskDelay(1);
        wait++;
    } while (wait < HPL_TEST_WAIT_LIMIT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_NOT_CREATED, ret);
    /* Let the idle task put the TCB back on the free list. */
    LOS_TaskDelay(20);

    /* The free list is LIFO, so task B reuses task A's slot: this exercises
     * the hpl-handle save/restore across the OsTaskStackFree memset. */
    g_testCount = 0;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF23b;
    taskParam.pcName = "HplDel23b";
    ret = LOS_TaskCreate(&taskBId, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(taskBId, taskAId, taskBId);

    wait = 0;
    while ((g_testCount != 2) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount);
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    /* Logs from both the deleted and the slot-reusing task are recorded. */
    argsNow = g_hplArgsEntryCnt;
    ICUNIT_ASSERT_EQUAL(argsNow - argsBefore, HPL_TEST_LOG_COUNT * 2, argsNow - argsBefore);


    return LOS_OK;
}

VOID ItLosHpl023(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHpl023", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}

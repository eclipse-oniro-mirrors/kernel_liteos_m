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

static VOID TaskF24a(VOID)
{
    UINT32 i;

    for (i = 0; i < HPL_TEST_LOG_COUNT; i++) {
        HP_LOG(HPL_DEBUG, "ck %u", i);
    }
    g_testCount = 1;
}

static VOID TaskF24b(VOID)
{
    UINT32 i;

    for (i = 0; i < HPL_TEST_LOG_COUNT; i++) {
        HP_LOG(HPL_DEBUG, "ck2 %u", i);
    }
    g_testCount = 2;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 wait = 0;
    UINT32 ckBefore;
    UINT32 ckNow;
    TSK_INIT_PARAM_S taskParam = {0};

    TestHplPrepare();
    g_testCount = 0;

    /* First burst: guarantees at least one checkpoint exists in the stream. */
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF24a;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplCk24a";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    while ((g_testCount != 1) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    ckNow = g_hplCheckpointCnt;
    ICUNIT_ASSERT_EQUAL((ckNow >= 1), 1, ckNow);
    ckBefore = ckNow;

    /* Second burst: the checkpoint flag is already set, no new checkpoint
     * may appear (uniqueness across the whole compressed stream). */
    g_testCount = 0;
    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF24b;
    taskParam.pcName = "HplCk24b";
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    wait = 0;
    while ((g_testCount != 2) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount);
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    ckNow = g_hplCheckpointCnt;
    ICUNIT_ASSERT_EQUAL(ckNow, ckBefore, ckNow - ckBefore);


    return LOS_OK;
}

VOID ItLosHpl024(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHpl024", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}

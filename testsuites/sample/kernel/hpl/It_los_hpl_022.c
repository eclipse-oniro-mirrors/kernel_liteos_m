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

/* Worker priority above the compression task (prio HPL_COMPRESS_TASK_PRIORITY,
 * default 2): once the staging buffer is full the writer keeps running, the
 * remaining entries are abandoned and later reported through the abandoned
 * log entry (HPL_ABANDONED_LOG_ENTRY_MAGIC) in the compressed stream. */
#define HPL_TEST_OVF_PRIO       1
#define HPL_TEST_OVF_COUNT      500
#define HPL_TEST_OVF_STAGING    256

static VOID TaskF22(VOID)
{
    UINT32 i;

    (VOID)LOS_TaskHplSizeSet(LOS_CurTaskIDGet(), HPL_TEST_OVF_STAGING);
    for (i = 0; i < HPL_TEST_OVF_COUNT; i++) {
        HP_LOG(HPL_DEBUG, "ovf %u %s", i, "padpad");
    }
    g_testCount = 1;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 wait = 0;
    UINT32 abdBefore;
    UINT32 abdNow;
    TSK_INIT_PARAM_S taskParam = {0};

    TestHplPrepare();
    abdBefore = g_hplAbandonedCnt;
    g_testCount = 0;

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF22;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplOvf022";
    taskParam.usTaskPrio = HPL_TEST_OVF_PRIO;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    while ((g_testCount != 1) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    /* Overflowed entries are reported instead of being lost silently. */
    abdNow = g_hplAbandonedCnt;
    ICUNIT_ASSERT_EQUAL((abdNow > abdBefore), 1, abdNow - abdBefore);


    return LOS_OK;
}

VOID ItLosHpl022(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHpl022", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}

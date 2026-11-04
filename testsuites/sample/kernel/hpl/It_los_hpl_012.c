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

static VOID TaskF12Silent(VOID)
{
    UINT32 i;

    for (i = 0; i < HPL_TEST_LOG_COUNT; i++) {
        HP_LOG(HPL_DEBUG, "flt silent %u", i);
    }
    g_testCount = 1;
}

static VOID TaskF12Open(VOID)
{
    UINT32 i;

    for (i = 0; i < HPL_TEST_LOG_COUNT; i++) {
        HP_LOG(HPL_DEBUG, "flt open %u", i);
    }
    g_testCount = 2;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 wait = 0;
    UINT32 bytesBefore;
    UINT32 bytesNow;
    TSK_INIT_PARAM_S taskParam = {0};

    TestHplPrepare();

    /* HPL_SILENT: OsHplLogger filters everything before the staging write. */
    ret = LOS_HplSetLogLevel(HPL_SILENT);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    bytesBefore = g_hplHookBytes;
    g_testCount = 0;

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF12Silent;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplFlt12a";
    taskParam.usTaskPrio = TASK_PRIO_TEST - 1;
    taskParam.uwResved = LOS_TASK_STATUS_DETACHED;
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    wait = 0;
    while ((g_testCount != 1) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    bytesNow = g_hplHookBytes;
    ICUNIT_ASSERT_EQUAL(bytesNow, bytesBefore, bytesNow - bytesBefore);

    /* Level opened: the same amount of logs now reaches the hook. */
    ret = LOS_HplSetLogLevel(HPL_TEST_LOG_LEVEL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    bytesBefore = g_hplHookBytes;
    g_testCount = 0;

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF12Open;
    taskParam.pcName = "HplFlt12b";
    ret = LOS_TaskCreate(&g_testTaskID01, &taskParam);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    wait = 0;
    while ((g_testCount != 2) && (wait < HPL_TEST_WAIT_LIMIT)) {
        LOS_TaskDelay(1);
        wait++;
    }
    ICUNIT_ASSERT_EQUAL(g_testCount, 2, g_testCount);
    LOS_TaskDelay(HPL_TEST_DRAIN_TICKS);

    bytesNow = g_hplHookBytes;
    ICUNIT_ASSERT_EQUAL((bytesNow > bytesBefore), 1, bytesNow - bytesBefore);


    return LOS_OK;
}

VOID ItLosHpl012(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHpl012", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}

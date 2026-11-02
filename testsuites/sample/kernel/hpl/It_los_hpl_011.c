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

static VOID TaskF11(VOID)
{
    UINT32 i;
    VOID *ptr = (VOID *)&i;

    for (i = 0; i < HPL_TEST_LOG_COUNT; i++) {
        HP_LOG(HPL_DEBUG, "hpl t%u 0x%x %u %c %s %p %ld", i, 0x1234ABCD, 7, 'A', "hplstr", ptr, 99L);
    }
    g_testCount = 1;
}

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 wait = 0;
    UINT32 bytesBefore;
    UINT32 bytesNow;
    UINT32 argsBefore;
    UINT32 argsNow;
    UINT32 ckNow;
    TSK_INIT_PARAM_S taskParam = {0};

    TestHplPrepare();
    bytesBefore = g_hplHookBytes;
    argsBefore = g_hplArgsEntryCnt;
    g_testCount = 0;

    taskParam.pfnTaskEntry = (TSK_ENTRY_FUNC)TaskF11;
    taskParam.uwStackSize = TASK_STACK_SIZE_TEST;
    taskParam.pcName = "HplRec011";
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

    /* Compressed output reached the record hook. */
    bytesNow = g_hplHookBytes;
    ICUNIT_ASSERT_EQUAL((bytesNow > bytesBefore), 1, bytesNow - bytesBefore);

    /* Every log produces exactly one compressed args entry. */
    argsNow = g_hplArgsEntryCnt;
    ICUNIT_ASSERT_EQUAL(argsNow - argsBefore, HPL_TEST_LOG_COUNT, argsNow - argsBefore);

    /* The first compression batch carries a checkpoint. */
    ckNow = g_hplCheckpointCnt;
    ICUNIT_ASSERT_EQUAL((ckNow >= 1), 1, ckNow);


    return LOS_OK;
}

VOID ItLosHpl011(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHpl011", TestCase, TEST_LOS, TEST_HPL, TEST_LEVEL0, TEST_FUNCTION);
}

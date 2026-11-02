/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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
#include "It_los_swtmr.h"


static VOID Case1(UINTPTR arg)
{
    UINT32 index;

    TestHwiClear(HWI_NUM_TEST);

    for (index = 0; index < TEST_HWI_RUNTIME; index++) {
    }
    g_testCount++;
}

static VOID Case2(VOID)
{
    g_testCount++;
}

static VOID Case3(VOID)
{
    g_testCount++;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 swtmrId1;
    UINT32 swtmrId2;
    UINT32 waitLoop;
    g_testCount = 0;

    // 1, Timeout interval of a periodic software timer.
    ret = LOS_SwtmrCreate(1, LOS_SWTMR_MODE_PERIOD | LOS_SWTMR_ALIGN_FLAG, (SWTMR_PROC_FUNC)Case3, &swtmrId1, 0xffff);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    // 2, Timeout interval of a periodic software timer.
    ret = LOS_SwtmrCreate(2, LOS_SWTMR_MODE_PERIOD | LOS_SWTMR_ALIGN_FLAG, (SWTMR_PROC_FUNC)Case2, &swtmrId2, 0xffff);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_HwiCreate(HWI_NUM_TEST, 1, 0, (HWI_PROC_FUNC)Case1, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    LOS_SwtmrStart(swtmrId1);
    LOS_SwtmrStart(swtmrId2);

    TestHwiTrigger(HWI_NUM_TEST);

    // 10, Here, check that g_testCount is smaller than this .
    /* Bounded wait: see It_los_swtmr_033.c. Periodic timers keep counting
     * while we yield, so accept a small overshoot above 10. */
    for (waitLoop = 0; (g_testCount < 10) && (waitLoop < TEST_WAIT_TIMEOUT); waitLoop++) {
        LOS_TaskDelay(1);
    }
    ICUNIT_GOTO_WITHIN_EQUAL(g_testCount, 10, 20, g_testCount, EXIT);

    LOS_SwtmrDelete(swtmrId2);
    LOS_SwtmrDelete(swtmrId1);

    TestHwiDelete(HWI_NUM_TEST);

    return LOS_OK;
EXIT:
    LOS_SwtmrDelete(swtmrId2);
    LOS_SwtmrDelete(swtmrId1);
    TestHwiDelete(HWI_NUM_TEST);
    return LOS_OK;
}

VOID ItLosSwtmr034(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosSwtmr034", Testcase, TEST_LOS, TEST_SWTMR, TEST_LEVEL1, TEST_FUNCTION);
}


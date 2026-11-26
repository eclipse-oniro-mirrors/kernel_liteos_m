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
#include "It_los_mux.h"

#ifdef LOSCFG_KERNEL_SMP

static VOID Hwi01(VOID)
{
    UINT32 ret;

    ret = LOS_MuxPost(g_mutexTest1);
    ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_ERRNO_MUX_PEND_INTERR, ret);

    TestHwiClear(HWI_NUM_TEST);

    g_testCount++;
}

static VOID Task01(VOID)
{

    TestHwiTrigger(HWI_NUM_TEST);

    g_testCount++;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 currCpuid;
    g_testCount = 0;
    TSK_INIT_PARAM_S task = { 0 };
    currCpuid = (ArchCurrCpuid() + 1) % (LOSCFG_KERNEL_CORE_NUM);

    ret = LOS_MuxCreate(&g_mutexTest1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_HwiCreate(HWI_NUM_TEST, 0, 0, (HWI_PROC_FUNC)Hwi01, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    LOS_HwiSetAffinity(HWI_NUM_TEST, CPUID_TO_AFFI_MASK(currCpuid));

    task.pfnTaskEntry = (TSK_ENTRY_FUNC)Task01;
    task.pcName = "Test Case 1";
    task.uwStackSize = TASK_STACK_SIZE_TEST;
    task.usTaskPrio = TASK_PRIO_TEST_TASK;
    task.uwResved = LOS_TASK_STATUS_DETACHED;
    task.usCpuAffiMask = CPUID_TO_AFFI_MASK(currCpuid);

    ret = LOS_TaskCreate(&g_testTaskID01, &task);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_MuxPend(g_mutexTest1, LOS_WAIT_FOREVER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    TEST_BUSY_DELAY(g_testCount, 2, 100);                      // 100, 2, delay for Timing control.
    ICUNIT_GOTO_EQUAL(g_testCount, 2, g_testCount, EXIT); // 2, here assert the result.

    TestHwiDelete(HWI_NUM_TEST);

    ret = LOS_MuxPost(g_mutexTest1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = LOS_MuxDelete(g_mutexTest1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
EXIT:
    TestHwiDelete(HWI_NUM_TEST);
    LOS_TaskDelete(g_testTaskID01);
    LOS_MuxPost(g_mutexTest1);
    LOS_MuxDelete(g_mutexTest1);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

VOID ItSmpLosMux003(void)
{
#ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosMux003", Testcase, TEST_LOS, TEST_FN_KERNEL_MUTEX, TEST_LEVEL0, TEST_FUNCTION);
#endif
}


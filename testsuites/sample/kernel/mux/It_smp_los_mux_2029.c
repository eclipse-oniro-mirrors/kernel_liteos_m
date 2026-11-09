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
static UINT32 g_ret = 0xff;

static VOID TaskF01(VOID)
{
    UINT32 ret;

    LOS_AtomicInc((Atomic *)&g_testCount);

    ret = LOS_MuxPend(g_mutexkernelTest, LOS_WAIT_FOREVER); // pend on current cpu
    ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);

    LOS_AtomicInc((Atomic *)&g_testCount);

    ret = LOS_MuxPost(g_mutexkernelTest);
    ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);

    TestDumpCpuid();
    return;
}

static VOID TaskF02(VOID)
{
    UINT32 ret, i;

    LOS_AtomicInc((Atomic *)&g_testCount);

    for (i = 0; i < TRandom() % 500; i++) { // 500, get rand number in 500.
    }

    ret = LOS_TaskDelete(g_testTaskID01);
    g_ret = ret;

    LOS_AtomicInc((Atomic *)&g_testCount);

    TestDumpCpuid();
    return;
}
static UINT32 Testcase(VOID)
{
    UINT32 ret, currCpuid, i, j;
    TSK_INIT_PARAM_S testTask = {0};

    g_testCount = 0;
    currCpuid = (ArchCurrCpuid() + 1) % (LOSCFG_KERNEL_CORE_NUM);

    for (i = 0; i < LOOP; i++) {
        PRINT_DEBUG("i = %d\n", i);
        ret = LOS_MuxCreate(&g_mutexkernelTest);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

        ret = LOS_MuxPend(g_mutexkernelTest, LOS_WAIT_FOREVER);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

        TEST_TASK_PARAM_INIT_AFFI(testTask, "it_smp_event_037_task1", TaskF01, TASK_PRIO_TEST_TASK - 1,
            CPUID_TO_AFFI_MASK(ArchCurrCpuid())); // current cpu
        ret = LOS_TaskCreate(&g_testTaskID01, &testTask);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

        /* must be checked before task F02 (which deletes F01) is created */
        {
            UINT32 waitPend = 0;
            while (((OS_TCB_FROM_TID(g_testTaskID01)->taskStatus & OS_TASK_STATUS_PEND) == 0) &&
                   (waitPend++ < 100)) {
                LOS_TaskDelay(1);
            }
        }
        ret = OS_TCB_FROM_TID(g_testTaskID01)->taskStatus;
        ICUNIT_GOTO_NOT_EQUAL(ret & OS_TASK_STATUS_PEND, 0, ret, EXIT);

        TEST_TASK_PARAM_INIT_AFFI(testTask, "it_smp_event_037_task2", TaskF02, TASK_PRIO_TEST_TASK - 1,
            CPUID_TO_AFFI_MASK(currCpuid)); // other cpu
        ret = LOS_TaskCreate(&g_testTaskID02, &testTask);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

        do {
            __asm__ volatile("nop");
        } while (g_testCount == 1); // wait for task f02

        for (j = 0; j < TRandom() % 500; j++) { // 500, get rand number in 500.
        }

        ret = LOS_MuxPost(g_mutexkernelTest); // try to wake task f01
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

        LOS_TaskDelay(10); // 10, delay for Timing control.

        ret = OS_TCB_FROM_TID(g_testTaskID01)->taskStatus;
        ICUNIT_GOTO_NOT_EQUAL(ret & OS_TASK_STATUS_UNUSED, 0, ret, EXIT);

        if ((g_ret != LOS_OK) && (g_ret != LOS_ERRNO_TSK_NOT_CREATED)) {
            ICUNIT_GOTO_EQUAL(1, 0, g_ret, EXIT);
        }

        PRINT_DEBUG("g_testCount = %d ,g_ret = 0x%x\n", g_testCount, g_ret);

        LOS_MuxDelete(g_mutexkernelTest);
    }
EXIT:
    LOS_TaskPriSet(g_testTskHandle, TASK_PRIO_TEST_TASK); /* recovery test task's prio */

    LOS_TaskDelete(g_testTaskID01);
    LOS_TaskDelete(g_testTaskID02);
    LOS_MuxDelete(g_mutexkernelTest);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

VOID ItSmpLosMux2029(VOID) // IT_Layer_ModuleORFeature_No
{
    #ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosMux2029", Testcase, TEST_LOS, TEST_FN_KERNEL_MUTEX, TEST_LEVEL2, TEST_FUNCTION);
    #endif
}

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

#include "It_los_task.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cpluscplus */
#endif /* __cpluscplus */

/*
 * Ported from liteos_a testsuites task/smp It_smp_los_task_033.c
 * (TEST_FULL tier). Adaptations: LOSCFG_KERNEL_SMP body gate (UP builds
 * compile an empty registration stub); WFI/Dmb -> inline asm; atomic
 * counter arg cast. Shared helpers live in osTest.c.
 */
#ifdef LOSCFG_KERNEL_SMP

static void TaskF01(void)
{
    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);
    while (1) {
    }
}

static void TaskF02Preempt(void)
{
    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);
    while (1) {
    }
}

static UINT32 Testcase(void)
{
    (VOID)TaskF02Preempt; /* keep referenced: liteos_a task-entry candidate, uncalled here */
    UINT32 ret;
    TSK_INIT_PARAM_S task1 = { 0 };
    UINT32 testTaskIDSmp[LOSCFG_KERNEL_CORE_NUM];

    g_testCount = 0;
    // 2, It is used to calculate a priority relative to TASK_PRIO_TEST_TASK.
    TEST_TASK_PARAM_INIT(task1, "it_smp_task_033", (TSK_ENTRY_FUNC)TaskF01, TASK_PRIO_TEST_TASK + 2);
    int i;
    (VOID)i;

    for (i = 0; i < LOSCFG_KERNEL_CORE_NUM - 1; i++) {
        /* take control of every cores */
        // 2, It is used to calculate a priority relative to TASK_PRIO_TEST_TASK.
        task1.usTaskPrio = TASK_PRIO_TEST_TASK + 2 + i;
        task1.usCpuAffiMask = 0;
        ret = LOS_TaskCreate(&testTaskIDSmp[i], &task1);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    }

        {
        UINT64 __tEnd = TestTickCountGet() + 2;
        while (TestTickCountGet() < __tEnd) { }
    } // 2, set delay time
    task1.usTaskPrio = TASK_PRIO_TEST_TASK + 1;
    ret = LOS_TaskCreate(&g_testTaskID01, &task1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* wait for task to schedule */
        {
        UINT64 __tEnd = TestTickCountGet() + 10;
        while (TestTickCountGet() < __tEnd) { }
    } // 10, set delay time

    /* take this core back to control */
    ICUNIT_GOTO_EQUAL(g_testCount, LOSCFG_KERNEL_CORE_NUM - 1 + 1, g_testCount, EXIT);

    ret = OS_TCB_FROM_TID(g_testTaskID01)->taskStatus & OS_TASK_STATUS_CHECK_MASK;
    ICUNIT_GOTO_EQUAL(ret, OS_TASK_STATUS_RUNNING | OS_TASK_STATUS_DETACHED, ret, EXIT);

    // 2, It is used to calculate number of cycles
    for (i = 0; i < LOSCFG_KERNEL_CORE_NUM - 2; i++) {
        ret = OS_TCB_FROM_TID(testTaskIDSmp[i])->taskStatus & OS_TASK_STATUS_CHECK_MASK;
        ICUNIT_GOTO_EQUAL(ret, OS_TASK_STATUS_RUNNING | OS_TASK_STATUS_DETACHED, ret, EXIT);
    }
    // 2, It is used to calculate number of cycles
    ret = OS_TCB_FROM_TID(testTaskIDSmp[LOSCFG_KERNEL_CORE_NUM - 2])->taskStatus & OS_TASK_STATUS_CHECK_MASK;
    ICUNIT_GOTO_EQUAL(ret, OS_TASK_STATUS_READY | OS_TASK_STATUS_DETACHED, ret, EXIT);

    for (i = 0; i < LOSCFG_KERNEL_CORE_NUM - 1; i++) {
        ret = LOS_TaskDelete(testTaskIDSmp[i]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    }

    ret = LOS_TaskDelete(g_testTaskID01);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ICUNIT_ASSERT_EQUAL(g_testCount, LOSCFG_KERNEL_CORE_NUM, g_testCount);

    return LOS_OK;

EXIT:
    for (i = 0; i < LOSCFG_KERNEL_CORE_NUM - 1; i++) {
        LOS_TaskDelete(testTaskIDSmp[i]);
    }
    LOS_TaskDelete(g_testTaskID01);

    return LOS_NOK;
}

#endif /* LOSCFG_KERNEL_SMP */

void ItSmpLosTask033(void)
{
#ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosTask033", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */

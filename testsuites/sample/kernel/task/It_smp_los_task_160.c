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
 * Ported from liteos_a testsuites task/smp It_smp_los_task_160.c
 * (TEST_FULL tier). Adaptations: LOSCFG_KERNEL_SMP body gate (UP builds
 * compile an empty registration stub); WFI/Dmb -> inline asm; atomic
 * counter arg cast. Shared helpers live in osTest.c.
 */
#ifdef LOSCFG_KERNEL_SMP

static void TaskF01(void)
{
    UINT32 ret;
    (VOID)ret; /* -Werror */
    ICUNIT_ASSERT_EQUAL_VOID(g_testCount, 0, g_testCount);

    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);

    while (1) {
    }

    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);
}

static UINT32 Testcase(void)
{
    UINT32 ret;
    TSK_INIT_PARAM_S task1 = { 0 };

    g_testCount = 0;
    TEST_TASK_PARAM_INIT_AFFI(task1, "it_smp_task_160", (TSK_ENTRY_FUNC)TaskF01, TASK_PRIO_TEST_TASK - 1, 0);

    task1.usCpuAffiMask = CPUID_TO_AFFI_MASK(LOSCFG_KERNEL_CORE_NUM);
    ret = LOS_TaskCreate(&g_testTaskID01, &task1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

        {
        UINT64 __tEnd = TestTickCountGet() + 10;
        while (TestTickCountGet() < __tEnd) { }
    } // 10, set delay time

    ICUNIT_ASSERT_EQUAL(g_testCount, 0, g_testCount);

    ret = OS_TCB_FROM_TID(g_testTaskID01)->taskStatus & OS_TASK_STATUS_CHECK_MASK;
    ICUNIT_GOTO_EQUAL(ret, OS_TASK_STATUS_UNUSED, ret, EXIT);

    ret = LOS_TaskResume(g_testTaskID01);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;

EXIT:

    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

void ItSmpLosTask160(void)
{
#ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosTask160", Testcase, TEST_LOS, TEST_TASK, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */

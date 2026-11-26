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

#include "It_los_event.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cpluscplus */
#endif /* __cpluscplus */

/*
 * Ported from liteos_a testsuites event It_smp_*.c.
 * Adaptations: LOSCFG_KERNEL_SMP body gate (UP builds compile an empty
 * registration stub); HalIrqSetAffinity -> LOS_HwiSetAffinity (public API,
 * GICv2 ITARGETSR byte write = liteos_a gic_v2.c:63 semantics);
 * TEST_HwiClear/TEST_HwiDelete -> TestHwiClear/TestHwiDelete; atomic
 * counter arg cast; g_pevent->g_pevent. Shared helpers live in osTest.c.
 */
#ifdef LOSCFG_KERNEL_SMP

static VOID TaskF01(VOID)
{
    UINT32 ret;

    ICUNIT_ASSERT_EQUAL_VOID(g_testCount, 0, g_testCount);
    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);
#ifdef LOSCFG_KERNEL_SMP
    /* SMP-adapt: asynchronously dispatched tasks (MpSchedule broadcast
     * on create) must finish before destroy, or the pend list is still
     * non-empty and destroy fails with SHOULD_NOT_DESTROYED. */
    LOS_TaskDelay(100);
#endif
    ret = LOS_EventDestroy(&g_pevent);
    ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);

    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);
    TestDumpCpuid();
    return;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    TSK_INIT_PARAM_S testTask;

    g_testCount = 0;
    g_pevent.uwEventID = 0;

    ret = LOS_EventInit(&g_pevent);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    TEST_TASK_PARAM_INIT_AFFI(testTask, "it_event_001_task", TaskF01, TASK_PRIO_TEST - 1,
        CPUID_TO_AFFI_MASK(ArchCurrCpuid()));
    ret = LOS_TaskCreate(&g_testTaskID01, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* SMP: the task delays 100 ticks before destroying; wait for it instead
     * of asserting immediately (a premature EXIT-side destroy would race
     * the task-side destroy). */
    TEST_BUSY_DELAY(g_testCount, 2, 1000);

    ICUNIT_GOTO_EQUAL(g_testCount, 2, g_testCount, EXIT); // 2, Here, assert that g_testCount is equal to 2.

EXIT:
    LOS_TaskDelete(g_testTaskID01);
    LOS_EventDestroy(&g_pevent);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

VOID ItSmpLosEvent001(VOID) // IT_Layer_ModuleORFeature_No
{
#ifdef LOSCFG_KERNEL_SMP
        TEST_ADD_CASE("ItSmpLosEvent001", Testcase, TEST_LOS, TEST_FN_KERNEL_EVENT, TEST_LEVEL2, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */

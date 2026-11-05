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
 * Ported from liteos_a testsuites task/smp It_smp_los_task_141.c.
 * Adaptations: LOSCFG_KERNEL_SMP body gate (UP builds compile an empty
 * registration stub); HalIrqSetAffinity -> LOS_HwiSetAffinity (public API,
 * FBB los_hwi.c shape, GICv2 ITARGETSR byte write = liteos_a gic_v2.c:63
 * semantics); TEST_HwiClear/TEST_HwiDelete -> TestHwiClear/TestHwiDelete
 * (single soft-pend per trigger: GIC ACK consumes pending, SPI delivery
 * per osTest.h A7 note); atomic counter arg cast. Shared helpers live in
 * osTest.c.
 */
#ifdef LOSCFG_KERNEL_SMP

static void TaskF01(void)
{
    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);
}

static void HwiF01(void)
{
    UINT32 ret;

    TSK_INIT_PARAM_S task1 = { 0 };

    TestHwiClear(HWI_NUM_TEST);

    TEST_TASK_PARAM_INIT(task1, "it_smp_task_141", (TSK_ENTRY_FUNC)TaskF01, TASK_PRIO_TEST_TASK - 1);

    ICUNIT_ASSERT_EQUAL_VOID(g_testCount, 0, g_testCount);

    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);

    task1.usCpuAffiMask = CPUID_TO_AFFI_MASK((ArchCurrCpuid() - 1) % (LOSCFG_KERNEL_CORE_NUM));
    ret = LOS_TaskCreate(&g_testTaskID01, &task1);
    ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);
}

static UINT32 Testcase(void)
{
    UINT32 ret;

    TSK_INIT_PARAM_S task1 = { 0 };
    (VOID)task1;

    g_testCount = 0;

    LOS_TaskLock();

    ret = LOS_HwiCreate(HWI_NUM_TEST, 1, 0, (HWI_PROC_FUNC)HwiF01, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    (VOID)LOS_HwiSetAffinity(HWI_NUM_TEST, CPUID_TO_AFFI_MASK((ArchCurrCpuid() + 1) % (LOSCFG_KERNEL_CORE_NUM))); // other cpu

    TestHwiTrigger(HWI_NUM_TEST);

        {
        UINT64 __tEnd = TestTickCountGet() + 5;
        while (TestTickCountGet() < __tEnd) { }
    } // 5, set delay time

    TestHwiDelete(HWI_NUM_TEST);

    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT);

    LOS_TaskUnlock();
    ICUNIT_GOTO_EQUAL(g_testCount, 2, g_testCount, EXIT);

    return LOS_OK;

EXIT:
    return LOS_NOK;
}

#endif /* LOSCFG_KERNEL_SMP */

void ItSmpLosTask141(void)
{
#ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosTask141", Testcase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */

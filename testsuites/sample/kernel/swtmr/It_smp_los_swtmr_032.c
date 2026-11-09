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

#include "It_los_swtmr.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cpluscplus */
#endif /* __cpluscplus */

/*
 * Ported from liteos_a testsuites swtmr/smp It_smp_*.c.
 * Adaptations: LOSCFG_KERNEL_SMP body gate (UP builds compile an empty
 * registration stub); HalIrqSetAffinity -> LOS_HwiSetAffinity;
 * TEST_HwiClear/TEST_HwiDelete -> TestHwiClear/TestHwiDelete; atomic
 * counter arg cast; g_usSwTmrID -> g_usSwTmrID (UINT32, see osTest.h).
 * Shared helpers live in osTest.c.
 */
#ifdef LOSCFG_KERNEL_SMP

static UINT32 g_sztmrId[LOSCFG_KERNEL_CORE_NUM] = {0};
static UINT32 g_sztskId[LOSCFG_KERNEL_CORE_NUM] = {0};
static VOID SwtmrF01(void)
{
    (VOID)LOS_AtomicInc((Atomic *)&g_testCount);
}

static VOID TaskF01(UINT32 index)
{
    UINT32 ret, i, j;

    for (i = 0; i < LOOP; i++) {
        for (j = 0; j < TRandom() % 100; j++) { // 100, Gets a random number between 0 and 100
        }

        ret = LOS_SwtmrCreate(g_testPeriod, LOS_SWTMR_MODE_PERIOD,
                              (SWTMR_PROC_FUNC)SwtmrF01, &g_sztmrId[index], 0);
        ICUNIT_ASSERT_EQUAL_VOID(ret, LOS_OK, ret);

        ret = LOS_SwtmrStart(g_sztmrId[index]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

        LOS_TaskDelay(g_testPeriod + 3); // +3, have enough time to wait alarm.

        for (j = 0; j < TRandom() % 100; j++) { // 100, Gets a random number between 0 and 100
        }

        ret = LOS_SwtmrStop(g_sztmrId[index]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

        ret = LOS_SwtmrDelete(g_sztmrId[index]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    }
    TestDumpCpuid();
    return;
EXIT:
    for (i = 0; i < LOSCFG_KERNEL_CORE_NUM; i++) {
        LOS_SwtmrDelete(g_sztmrId[i]);
    }

    return;
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    #ifdef LOSCFG_KERNEL_SMP
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());
    #endif
    UINT32 ret;
    UINT32 currCpuid;
    (VOID)currCpuid;
    UINT32 i;

    g_testCount = 0;
    g_testPeriod = 10; // period is 10

    for (i = 0; i < LOSCFG_KERNEL_CORE_NUM; i++) {
        TEST_TASK_PARAM_INIT_AFFI(testTask, "it_swtmr_032_task1", TaskF01, TASK_PRIO_TEST_SWTMR - 1,
            CPUID_TO_AFFI_MASK((ArchCurrCpuid() + i + 1) %
            LOSCFG_KERNEL_CORE_NUM)); // let current cpu 's task create at the last
        testTask.uwArg = i;
        ret = LOS_TaskCreate(&g_sztskId[i], &testTask);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    }

    LOS_TaskDelay(g_testPeriod * LOOP * 2 + 1000);

    ICUNIT_GOTO_EQUAL(g_testCount, LOSCFG_KERNEL_CORE_NUM * LOOP, g_testCount, EXIT);

    return LOS_OK;

EXIT:
    for (i = 0; i < LOSCFG_KERNEL_CORE_NUM; i++) {
        LOS_SwtmrDelete(g_sztmrId[i]);
        LOS_TaskDelete(g_sztskId[i]);
    }
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

VOID ItSmpLosSwtmr032(VOID)
{
#ifdef LOSCFG_KERNEL_SMP
        TEST_ADD_CASE("ItSmpLosSwtmr032", Testcase, TEST_LOS, TEST_FN_KERNEL_SWTMR, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */


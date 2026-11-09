/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor its contributors may be used to endorse
 *    or promote products derived from this software without specific prior written
 *    permission.
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

/* Ported from liteos_a testsuites kernel/sample/kernel_base/core/task/smp/
 * It_smp_los_task_002.c (Task Deletion Across Cores): a task pinned to the
 * OTHER core loops in WFI; deleting it cross-core must return LOS_OK (async
 * KILL signal + targeted IPI route), and after the target core processes
 * the signal a second delete reports NOT_CREATED.
 *
 * Adaptation: WFI -> inline asm (cortex_a_r provides no wfi() C function;
 * the barrier.h WFI() macro would reference it under -marm). */

#include "osTest.h"
#include "It_los_task.h"

/* SMP-only case: body gated so UP builds compile an empty registration
 * stub (currCpuid would be unused under the UP AFFI macro anyway). */
#ifdef LOSCFG_KERNEL_SMP

static VOID TaskF01(VOID)
{
    while (1) {
        __asm__ volatile("wfi");
    }
}

static UINT32 Testcase(VOID)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, currCpuid;
    UINT32 testid = 0xff;

    /* make sure that created test task is definitely on another core */
    currCpuid = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    TEST_TASK_PARAM_INIT_AFFI(testTask, "it_smp_task_002", TaskF01,
        OS_TASK_PRIORITY_LOWEST - 1, CPUID_TO_AFFI_MASK(currCpuid));

    ret = LOS_TaskCreate(&testid, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    LOS_TaskDelay(2); /* 2: let the target core dispatch it */

    ret = LOS_TaskDelete(testid);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

#ifndef LOSCFG_KERNEL_SMP_TASK_SYNC
    /* async route (D5: SMP_TASK_SYNC off): wait for the target core to
     * process the KILL signal, then the second delete must fail */
    LOS_TaskDelay(10); /* 10: ticks */
#endif
    ret = LOS_TaskDelete(testid);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_NOT_CREATED, ret);

    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

VOID ItSmpLosTask002(VOID)
{
#ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosTask002", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

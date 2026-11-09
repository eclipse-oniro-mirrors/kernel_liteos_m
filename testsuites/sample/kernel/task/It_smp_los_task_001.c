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
 * It_smp_los_task_001.c (Task Affinity): one task per core, each pinned via
 * usCpuAffiMask; every task sets its core's bit in g_testCount under a test
 * spinlock. PASS = every core dispatched its own affined task (dequeue-side
 * affinity filter + cross-core wake chain).
 *
 * Adaptations: Dmb() dropped - LOS_SpinLockSave/UnlockRestore already issue
 * the ordering barriers on armv7-a; entry signature follows the morpheus
 * test idiom (VOID f(VOID), cast through TSK_ENTRY_FUNC by the init macro).
 * Compiles under UP (macros degrade); registered only under SMP. */

#include "osTest.h"
#include "It_los_task.h"

/* SMP-only case: body gated so UP builds compile an empty registration
 * stub (registration in It_los_task.c is likewise SMP-gated). */
#ifdef LOSCFG_KERNEL_SMP

static SPIN_LOCK_INIT(g_TestSpin);

static VOID TaskF01(VOID)
{
    UINT32 intSave;

    LOS_SpinLockSave(&g_TestSpin, &intSave);
    g_testCount |= 1U << ArchCurrCpuid();
    LOS_SpinUnlockRestore(&g_TestSpin, intSave);
}

static UINT32 Testcase(VOID)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret;
    UINT32 testid;
    UINT32 coreIdx = 0;

    g_testCount = 0;

    while (coreIdx < LOSCFG_KERNEL_CORE_NUM) {
        TEST_TASK_PARAM_INIT_AFFI(testTask, "it_smp_task_001", TaskF01,
            TASK_PRIO_TEST_TASK + 1, CPUID_TO_AFFI_MASK(coreIdx));
        ret = LOS_TaskCreate(&testid, &testTask);
        ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
        coreIdx++;
    }

    LOS_TaskDelay(5); /* 5: let every core run its affined task */

    ICUNIT_ASSERT_EQUAL(g_testCount, LOSCFG_KERNEL_CPU_MASK, g_testCount);

    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

VOID ItSmpLosTask001(VOID)
{
#ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosTask001", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

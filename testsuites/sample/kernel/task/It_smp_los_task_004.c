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
 * It_smp_los_task_004.c (Task Created Unbinded): a task created with
 * affinity mask 0 must default to ALL cores (OsNewTaskInit) and run to
 * completion regardless of which core dispatches it. The child asserts the
 * shared counter is still 0 at entry, then increments; the driver
 * busy-waits for the increment.
 *
 * The child runs at a higher priority than the driver (TASK_PRIO_TEST_TASK
 * - 2), so on either core it preempts or wakes promptly. */

#include "osTest.h"
#include "It_los_task.h"

/* SMP-only case: body gated so UP builds compile an empty registration
 * stub (registration in It_los_task.c is likewise SMP-gated). */
#ifdef LOSCFG_KERNEL_SMP

static VOID TaskF01(VOID)
{
    ICUNIT_ASSERT_EQUAL_VOID(g_testCount, 0, g_testCount);
    g_testCount++;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    TSK_INIT_PARAM_S task1 = { 0 };

    g_testCount = 0;

    /* affi 0 = unbound: kernel must default to all cores */
    TEST_TASK_PARAM_INIT_AFFI(task1, "it_smp_task_004", (TSK_ENTRY_FUNC)TaskF01,
        TASK_PRIO_TEST_TASK - 2, 0);
    task1.usCpuAffiMask = CPUID_TO_AFFI_MASK((ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM);
    ret = LOS_TaskCreate(&g_testTaskID01, &task1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    TEST_BUSY_DELAY(g_testCount, 1, 1000);
    TEST_YIELD_WAIT_COUNT(20, 1); // M2R31: SMP cross-core wake race, bounded wait
    ICUNIT_ASSERT_EQUAL(g_testCount, 1, g_testCount);

    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

VOID ItSmpLosTask004(VOID)
{
#ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosTask004", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

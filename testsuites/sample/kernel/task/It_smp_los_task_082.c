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
 * Ported from liteos_a testsuites task/smp It_smp_los_task_082.c
 * (TEST_FULL tier). Adaptations: LOSCFG_KERNEL_SMP body gate (UP builds
 * compile an empty registration stub); WFI/Dmb -> inline asm; atomic
 * counter arg cast. Shared helpers live in osTest.c.
 */
#ifdef LOSCFG_KERNEL_SMP

static UINT32 Testcase(void)
{
    UINT32 ret;
    UINT32 gTestIdleTaskID;
    UINT32 gTestSwtmrTaskID;
    UINT32 cpuid = (ArchCurrCpuid() + 1) % (LOSCFG_KERNEL_CORE_NUM);

    gTestIdleTaskID = g_percpu[cpuid].idleTaskId;

    ret = LOS_TaskSuspend(gTestIdleTaskID);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK, ret);

#if (LOSCFG_BASE_CORE_SWTMR == 1)
    gTestSwtmrTaskID = g_percpu[cpuid].swtmrTaskId;

    ret = LOS_TaskSuspend(gTestSwtmrTaskID);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_TSK_OPERATE_SYSTEM_TASK, ret);
#endif

    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_SMP */

void ItSmpLosTask082(void)
{
#ifdef LOSCFG_KERNEL_SMP
    TEST_ADD_CASE("ItSmpLosTask082", Testcase, TEST_LOS, TEST_TASK, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */

/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* 用例简要描述: ItSmpLosAtomic004 64 位原子跨核并发自增（A7 32 位总线拆写风险实证） */
#include "it_los_atomic.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static Atomic64 g_atm64Val = 0;

static volatile INT32 g_atmDone = 0;

static void AtmF04(void)
{
    int i;
    for (i = 0; i < 1000; i++) {
        (void)LOS_Atomic64IncRet(&g_atm64Val);
    }
    LOS_AtomicInc((Atomic *)&g_atmDone);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    g_atm64Val = 0;
    g_atmDone = 0;
    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)AtmF04;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_atm_004";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    AtmF04();
    {
        UINT64 tEnd = TestTickCountGet() + 50;
        while (TestTickCountGet() < tEnd) { }
    }
    ICUNIT_ASSERT_EQUAL((INT32)LOS_Atomic64Read(&g_atm64Val), 2000, (INT32)LOS_Atomic64Read(&g_atm64Val));
    return LOS_OK;
}

VOID ItSmpLosAtomic004(void)
{
#if KERNEL_BUG_18_ATOMIC64_LOSS
    TEST_ADD_CASE("ItSmpLosAtomic004", Testcase, TEST_LOS, TEST_FN_KERNEL_DATASTRUCT, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
#endif


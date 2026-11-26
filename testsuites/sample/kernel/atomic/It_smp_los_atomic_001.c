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
/* 用例简要描述: ItSmpLosAtomic001 跨核并发自增零丢失（volatile 丢更新破坏路径的原子解法实证） */
#include "it_los_atomic.h"
#ifdef LOSCFG_KERNEL_SMP

static Atomic g_atmVal = 0;

static void AtmF01(void)
{
    int i;
    for (i = 0; i < 1000; i++) {
        (void)LOS_AtomicIncRet(&g_atmVal);
    }
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    g_atmVal = 0;
    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)AtmF01;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_atm_001";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    AtmF01();   /* 本核同量并发 */
    {
        UINT64 tEnd = TestTickCountGet() + 50;
        while (TestTickCountGet() < tEnd) { }
    }
    ICUNIT_ASSERT_EQUAL(LOS_AtomicRead(&g_atmVal), 2000, LOS_AtomicRead(&g_atmVal));
    return LOS_OK;
}

VOID ItSmpLosAtomic001(void)
{
    TEST_ADD_CASE("ItSmpLosAtomic001", Testcase, TEST_LOS, TEST_FN_KERNEL_DATASTRUCT, TEST_LEVEL1, TEST_FUNCTION);
}
#endif


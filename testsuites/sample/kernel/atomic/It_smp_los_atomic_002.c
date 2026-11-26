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
/* 用例简要描述: ItSmpLosAtomic002 双核同时 CAS 同一初值恰一成功（竞争唯一性） */
#include "it_los_atomic.h"
#ifdef LOSCFG_KERNEL_SMP

static Atomic g_atmVal = 0;
static volatile INT32 g_casWin = 0;
static volatile INT32 g_loserDone = 0;

static void AtmF02(void)
{
    /* 败方（晚到）：值已 1，CAS(1,0) 必失败 */
    if (LOS_AtomicCmpXchg32bits(&g_atmVal, 1, 0)) {
        LOS_AtomicInc((Atomic *)&g_casWin);
    }
    LOS_AtomicInc((Atomic *)&g_loserDone);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    g_atmVal = 0;
    g_casWin = 0;
    g_loserDone = 0;

    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)AtmF02;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_atm_002";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 胜方（先到）：初值 0，CAS(0,1) 必成功 */
    if (LOS_AtomicCmpXchg32bits(&g_atmVal, 1, 0)) {
        LOS_AtomicInc((Atomic *)&g_casWin);
    }
    {
        UINT64 tEnd = TestTickCountGet() + 50;
        while (g_loserDone == 0 && TestTickCountGet() < tEnd) { }
    }
    ICUNIT_ASSERT_EQUAL(g_casWin, 1, g_casWin);          /* 恰一胜：胜方成功+败方失败 */
    ICUNIT_ASSERT_EQUAL(LOS_AtomicRead(&g_atmVal), 1, LOS_AtomicRead(&g_atmVal));
    return LOS_OK;
}

VOID ItSmpLosAtomic002(void)
{
    TEST_ADD_CASE("ItSmpLosAtomic002", Testcase, TEST_LOS, TEST_FN_KERNEL_DATASTRUCT, TEST_LEVEL1, TEST_FUNCTION);
}
#endif

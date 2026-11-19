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
/* 用例简要描述: ItSmpLosHwiPersist01 SPI 路由持久态：绑核 N 后中断只在核 N 响应（ITARGETSR 持久） */
#include "It_smp_hwi.h"
#ifdef LOSCFG_KERNEL_SMP

static volatile UINT32 g_cntCoreA = 0;
static volatile UINT32 g_cntCoreB = 0;

static VOID HwiF06(UINT32 arg)
{
    TestHwiClear(HWI_NUM_TEST);
    if (ArchCurrCpuid() == 0) {
        g_cntCoreA = g_cntCoreA + 1;
    } else {
        g_cntCoreB = g_cntCoreB + 1;
    }
}

static void PersF06(void)
{
    /* 绑核 B 的帮手：等待中断到达（若路由正确，仅其所在核响应） */
    UINT64 tEnd = TestTickCountGet() + 100;
    while (TestTickCountGet() < tEnd) { }
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;
    volatile UINT32 *cntOther, *cntSelf;

    g_cntCoreA = 0;
    g_cntCoreB = 0;

    TestHwiClear(HWI_NUM_TEST);
    ret = LOS_HwiCreate(HWI_NUM_TEST, 1, 0, (HWI_PROC_FUNC)HwiF06, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSetAffinity(HWI_NUM_TEST, CPUID_TO_AFFI_MASK(other));   /* 显式绑另一核 */
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)PersF06;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_hwi_p06";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 本核触发（软 pend）：持久路由应让 ISR 只在 other 核执行 */
    TestHwiTrigger(HWI_NUM_TEST);
    {
        UINT64 tEnd = TestTickCountGet() + 150;
        while (TestTickCountGet() < tEnd) { }
    }
    cntOther = (other == 0) ? &g_cntCoreA : &g_cntCoreB;
    cntSelf  = (other == 0) ? &g_cntCoreB : &g_cntCoreA;
    ICUNIT_ASSERT_EQUAL(*cntOther, 1, *cntOther);   /* 路由核响应恰一次 */
    ICUNIT_ASSERT_EQUAL(*cntSelf, 0, *cntSelf);     /* 触发核不响应 */

    TestHwiDelete(HWI_NUM_TEST);
    return LOS_OK;
}

VOID ItSmpLosHwiPersist01(void)
{
    TEST_ADD_CASE("ItSmpLosHwiPersist01", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL2, TEST_FUNCTION);
}
#endif


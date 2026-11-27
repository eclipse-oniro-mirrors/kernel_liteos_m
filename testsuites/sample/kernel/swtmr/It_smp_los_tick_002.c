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
/* 用例简要描述: ItSmpLosTick002 软件定时器回调在启动核执行（每核 swtmr 任务归属） */
#include "It_los_swtmr.h"
#ifdef LOSCFG_KERNEL_SMP

static volatile UINT32 g_cbCpu = 0xFF;

static void SwtmrF02(UINT32 arg)
{
    g_cbCpu = ArchCurrCpuid();
}

static void TickF02(void)
{
    UINT32 swTmrId;
    UINT32 ret;
    ret = LOS_SWTMR_CREATE(10, LOS_SWTMR_MODE_ONCE, (SWTMR_PROC_FUNC)SwtmrF02, &swTmrId, 0);
    if (ret != LOS_OK) { return; }
    ret = LOS_SwtmrStart(swTmrId);
    if (ret != LOS_OK) { return; }
    /* 帮手在核 other 启动：回调应跑在核 other 的 swtmr 任务 */
    LOS_TaskDelay(30);
    (void)LOS_SwtmrDelete(swTmrId);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    g_cbCpu = 0xFF;
    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)TickF02;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_tick_002";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    {
        UINT64 tEnd = TestTickCountGet() + 200;
        while (g_cbCpu == 0xFF && TestTickCountGet() < tEnd) { }
    }
    ICUNIT_ASSERT_EQUAL(g_cbCpu, other, g_cbCpu);   /* 回调核 = 启动核 */
    return LOS_OK;
}

VOID ItSmpLosTick002(void)
{
    TEST_ADD_CASE("ItSmpLosTick002", Testcase, TEST_LOS, TEST_FN_KERNEL_SWTMR, TEST_LEVEL2, TEST_FUNCTION);
}
#endif


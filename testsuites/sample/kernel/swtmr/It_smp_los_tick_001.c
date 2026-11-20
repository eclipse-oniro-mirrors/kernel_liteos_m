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
/* 用例简要描述: ItSmpLosTick001 任务跨核延时到期唤醒（per-CPU 超时链 timerCpu 归属） */
#include "It_los_swtmr.h"
#ifdef LOSCFG_KERNEL_SMP

static volatile UINT32 g_wakeCnt = 0;

static void TickF01(void)
{
    UINT64 tStart;
    LOS_TaskDelay(10);
    g_wakeCnt = g_wakeCnt + 1;
    (void)tStart;
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT64 t0, t1;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    g_wakeCnt = 0;
    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)TickF01;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_tick_001";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);   /* 帮手绑另一核：delay 挂入其 timerCpu 链 */
    t0 = TestTickCountGet();
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    {
        UINT64 tEnd = t0 + 200;
        while (g_wakeCnt == 0 && TestTickCountGet() < tEnd) { }
    }
    t1 = TestTickCountGet();
    ICUNIT_ASSERT_EQUAL(g_wakeCnt, 1, g_wakeCnt);
    ICUNIT_ASSERT_EQUAL((t1 - t0 >= 9) && (t1 - t0 <= 200), 1, t1 - t0);   /* 到期才醒（SMP 跨核 tick 边界 ±1，问题 20261119 在册） */
    return LOS_OK;
}

VOID ItSmpLosTick001(void)
{
    TEST_ADD_CASE("ItSmpLosTick001", Testcase, TEST_LOS, TEST_FN_KERNEL_TIME, TEST_LEVEL1, TEST_FUNCTION);
}
#endif


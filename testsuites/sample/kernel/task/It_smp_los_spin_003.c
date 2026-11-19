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
/* 用例简要描述: ItSmpLosSpin003 LOS_SpinUnlock 后另一核等待者可继续 */
#include "It_los_task.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static SPIN_LOCK_S g_spinLock;
static volatile INT32 g_got = 0;

static void SpinF03(void)
{
    /* 自旋等待获取锁（另一核持锁中） */
    LOS_SpinLock(&g_spinLock);
    g_got = g_got + 1;
    LOS_SpinUnlock(&g_spinLock);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    LOS_SpinInit(&g_spinLock);
    g_got = 0;

    LOS_SpinLock(&g_spinLock);         /* 本核先持锁 */

    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)SpinF03;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_spin_003";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    {
        UINT64 tEnd = TestTickCountGet() + 5;
        while (TestTickCountGet() < tEnd) { }   /* 让帮手进入自旋 */
    }
    LOS_SpinUnlock(&g_spinLock);       /* 解锁：等待者应能拿到 */

    {
        UINT64 tEnd = TestTickCountGet() + 50;
        while (g_got == 0 && TestTickCountGet() < tEnd) { }
    }
    ICUNIT_ASSERT_EQUAL(g_got, 1, g_got);
    return LOS_OK;
}

VOID ItSmpLosSpin003(void)
{
#if KERNEL_BUG_17_SMP_SPIN_LAYOUT_CRASH
    TEST_ADD_CASE("ItSmpLosSpin003", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL2, TEST_FUNCTION);
#endif
}
#endif


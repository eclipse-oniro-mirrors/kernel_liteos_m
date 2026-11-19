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
/* 用例简要描述: ItSmpLosSpin001 自旋锁基本互斥（跨核双任务持锁计数精确） */
#include "It_los_task.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static SPIN_LOCK_S g_spinLock;
static volatile INT32 g_spinCnt = 0;

static volatile INT32 g_spinDone = 0;

static void SpinF01(void)
{
    int i;
    for (i = 0; i < 100; i++) {          /* 降频：持锁自增验证互斥语义即可，高频解锁=IPI 风暴（崩溃问题在册待查内核） */
        LOS_SpinLock(&g_spinLock);
        g_spinCnt = g_spinCnt + 1;
        LOS_SpinUnlock(&g_spinLock);
    }
    LOS_AtomicInc((Atomic *)&g_spinDone);
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    g_spinCnt = 0;
    g_spinDone = 0;
    LOS_SpinInit(&g_spinLock);

    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)SpinF01;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_spin_001";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    {
        int i;
        for (i = 0; i < 100; i++) {
            LOS_SpinLock(&g_spinLock);
            g_spinCnt = g_spinCnt + 1;
            LOS_SpinUnlock(&g_spinLock);
        }
    }
    {
        UINT64 tEnd = TestTickCountGet() + 200;
        while (g_spinDone == 0 && TestTickCountGet() < tEnd) { }
    }
    ICUNIT_ASSERT_EQUAL(g_spinDone, 1, g_spinDone);
    ICUNIT_ASSERT_EQUAL(g_spinCnt, 200, g_spinCnt);
    return LOS_OK;
}

VOID ItSmpLosSpin001(void)
{
#if KERNEL_BUG_17_SMP_SPIN_LAYOUT_CRASH
    TEST_ADD_CASE("ItSmpLosSpin001", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
#endif


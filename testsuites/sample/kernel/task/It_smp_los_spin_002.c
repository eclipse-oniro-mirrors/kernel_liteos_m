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
/* 用例简要描述: ItSmpLosSpin002 LOS_SpinTrylock 非阻塞语义（他核持锁立即失败） */
#include "It_los_task.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static SPIN_LOCK_S g_spinLock;
static volatile INT32 g_locked = 0;
static volatile INT32 g_tryRet = -1;

static void SpinF02(void)
{
    LOS_SpinLock(&g_spinLock);
    g_locked = 1;
    /* 持锁自旋一段，保证主任务有机会 Trylock */
    while (g_tryRet == -1) { }
    LOS_SpinUnlock(&g_spinLock);
    g_locked = 0;
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;
    UINT32 other = (ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM;

    LOS_SpinInit(&g_spinLock);
    g_locked = 0;
    g_tryRet = -1;

    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)SpinF02;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_spin_002";
    testTask.usCpuAffiMask = CPUID_TO_AFFI_MASK(other);
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    while (g_locked != 1) { }          /* 帮手已持锁 */
    g_tryRet = LOS_SpinTrylock(&g_spinLock);
    ICUNIT_ASSERT_NOT_EQUAL(g_tryRet, LOS_OK, g_tryRet);   /* 非阻塞：立即失败 */
    while (g_locked != 0) { }
    g_tryRet = LOS_SpinTrylock(&g_spinLock);
    ICUNIT_ASSERT_EQUAL(g_tryRet, LOS_OK, g_tryRet);       /* 已释放：可获取 */
    LOS_SpinUnlock(&g_spinLock);
    return LOS_OK;
}

VOID ItSmpLosSpin002(void)
{
#if KERNEL_BUG_17_SMP_SPIN_LAYOUT_CRASH
    TEST_ADD_CASE("ItSmpLosSpin002", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
#endif


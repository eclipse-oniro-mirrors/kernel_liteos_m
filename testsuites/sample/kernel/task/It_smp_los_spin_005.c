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
/* 用例简要描述: ItSmpLosSpin005 LOS_SpinUnlockNoSched 中断上下文安全解锁 */
#include "It_los_task.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static SPIN_LOCK_S g_spinLock;
static volatile UINT32 g_isrOk = 0;

static VOID HwiF05(UINT32 arg)
{
    TestHwiClear(HWI_NUM_TEST);
    LOS_SpinUnlockNoSched(&g_spinLock);   /* ISR 内安全解锁（不触发调度） */
    g_isrOk = 1;
}

static UINT32 Testcase(void)
{
    UINT32 ret;

    LOS_SpinInit(&g_spinLock);
    g_isrOk = 0;
    LOS_SpinLock(&g_spinLock);         /* 主任务持锁 */

    TestHwiClear(HWI_NUM_TEST);
    ret = LOS_HwiCreate(HWI_NUM_TEST, 1, 0, (HWI_PROC_FUNC)HwiF05, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#ifdef LOSCFG_KERNEL_SMP
    ret = LOS_HwiSetAffinity(HWI_NUM_TEST, CPUID_TO_AFFI_MASK(ArchCurrCpuid()));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#endif

    TestHwiTrigger(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(g_isrOk, 1, g_isrOk);       /* ISR 内安全完成解锁 */
    ICUNIT_ASSERT_EQUAL(LOS_SpinHeld(&g_spinLock), FALSE, LOS_SpinHeld(&g_spinLock));

    TestHwiDelete(HWI_NUM_TEST);
    return LOS_OK;
}

VOID ItSmpLosSpin005(void)
{
#if KERNEL_BUG_17_SMP_SPIN_LAYOUT_CRASH
    TEST_ADD_CASE("ItSmpLosSpin005", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL2, TEST_FUNCTION);
#endif
}
#endif


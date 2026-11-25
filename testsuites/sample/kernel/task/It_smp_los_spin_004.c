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
/* 用例简要描述: ItSmpLosSpin004 LOS_SpinLockSave 持锁期间本核中断被屏蔽 */
#include "It_los_task.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static SPIN_LOCK_S g_spinLock;
static volatile UINT32 g_isrCnt = 0;

static VOID HwiF04(UINT32 arg)
{
    TestHwiClear(HWI_NUM_TEST);
    g_isrCnt = g_isrCnt + 1;
}

static UINT32 Testcase(void)
{
    UINT32 ret, intSave;

    LOS_SpinInit(&g_spinLock);
    g_isrCnt = 0;
    TestHwiClear(HWI_NUM_TEST);
    ret = LOS_HwiCreate(HWI_NUM_TEST, 1, 0, (HWI_PROC_FUNC)HwiF04, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#ifdef LOSCFG_KERNEL_SMP
    ret = LOS_HwiSetAffinity(HWI_NUM_TEST, CPUID_TO_AFFI_MASK(ArchCurrCpuid()));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#endif

    LOS_SpinLockSave(&g_spinLock, &intSave);
    TestHwiTrigger(HWI_NUM_TEST);      /* 持锁+关中断期间触发：ISR 不得进入 */
    if (g_isrCnt != 0) {
        /* 失败也走清理（GOTO 模式）：!1298 spinlock.S 后此断言曾失败(实测 g_isrCnt=2)，
         * 问题 20261119_LockSave_ISR 行为变化在册——中断残留会连锁打掉后续 hwi 用例 */
        LOS_SpinUnlockRestore(&g_spinLock, intSave);
        TestHwiDelete(HWI_NUM_TEST);
        ICUNIT_ASSERT_EQUAL(g_isrCnt, 0, g_isrCnt);
        return LOS_OK;
    }
    LOS_SpinUnlockRestore(&g_spinLock, intSave);
    TestHwiTrigger(HWI_NUM_TEST);      /* 恢复后触发：ISR 进入 */
    TestHwiDelete(HWI_NUM_TEST);       /* 先清理再断言（防残留连锁） */
    ICUNIT_ASSERT_EQUAL(g_isrCnt, 1, g_isrCnt);
    return LOS_OK;
}

VOID ItSmpLosSpin004(void)
{
#if KERNEL_BUG_17_SMP_SPIN_LAYOUT_CRASH
    TEST_ADD_CASE("ItSmpLosSpin004", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
#endif


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
/* 用例简要描述: ItSmpLosSpin007 SPIN_LOCK_INIT 静态初始化宏路径正常可用 */
#include "It_los_task.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static SPIN_LOCK_INIT(g_staticLock);   /* 静态初始化宏路径 */

static UINT32 Testcase(void)
{
    ICUNIT_ASSERT_EQUAL(LOS_SpinHeld(&g_staticLock), FALSE, LOS_SpinHeld(&g_staticLock));
    LOS_SpinLock(&g_staticLock);
    ICUNIT_ASSERT_EQUAL(LOS_SpinHeld(&g_staticLock), TRUE, LOS_SpinHeld(&g_staticLock));
    LOS_SpinUnlock(&g_staticLock);
    ICUNIT_ASSERT_EQUAL(LOS_SpinHeld(&g_staticLock), FALSE, LOS_SpinHeld(&g_staticLock));
    return LOS_OK;
}

VOID ItSmpLosSpin007(void)
{
#if KERNEL_BUG_17_SMP_SPIN_LAYOUT_CRASH
    TEST_ADD_CASE("ItSmpLosSpin007", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL0, TEST_FUNCTION);
#endif
}
#endif


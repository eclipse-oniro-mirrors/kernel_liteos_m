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
/* 用例简要描述: ItSmpLosSpin006 LOS_SpinHeld 持锁状态查询 */
#include "It_los_task.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static UINT32 Testcase(void)
{
    SPIN_LOCK_S lock;
    LOS_SpinInit(&lock);

    ICUNIT_ASSERT_EQUAL(LOS_SpinHeld(&lock), FALSE, LOS_SpinHeld(&lock));
    LOS_SpinLock(&lock);
    ICUNIT_ASSERT_EQUAL(LOS_SpinHeld(&lock), TRUE, LOS_SpinHeld(&lock));
    LOS_SpinUnlock(&lock);
    ICUNIT_ASSERT_EQUAL(LOS_SpinHeld(&lock), FALSE, LOS_SpinHeld(&lock));
    return LOS_OK;
}

VOID ItSmpLosSpin006(void)
{
#if KERNEL_BUG_17_SMP_SPIN_LAYOUT_CRASH
    TEST_ADD_CASE("ItSmpLosSpin006", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL0, TEST_FUNCTION);
#endif
}
#endif


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
/* 用例简要描述: ItSmpLosTaskCreateAffi01 创建时亲和性掩码严格校验（!1298 65d37e3c） */
#include "It_los_task.h"
#ifdef LOSCFG_KERNEL_SMP

static void AffiF01(void)
{
    /* 空函数：本用例只验证 create 的掩码校验，成功创建的任务立即删除 */
}

static UINT32 Testcase(void)
{
    TSK_INIT_PARAM_S testTask;
    UINT32 ret, taskId;

    (void)memset_s(&testTask, sizeof(TSK_INIT_PARAM_S), 0, sizeof(TSK_INIT_PARAM_S));
    testTask.pfnTaskEntry = (TSK_ENTRY_FUNC)AffiF01;
    testTask.usTaskPrio = TASK_PRIO_TEST_TASK - 1;
    testTask.uwStackSize = LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE;
    testTask.pcName = "smp_tsk_affi";

    testTask.usCpuAffiMask = 0;                       /* 0=未指定：允许 */
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    LOS_TaskDelete(taskId);

#if KERNEL_BUG_20_DEPEND_1298
    /* 越界位拒绝依赖 !1298 严格掩码；主线宽松版（含有效位即通过）下跳过 */
    testTask.usCpuAffiMask = (UINT16)0xFFFF;
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
#endif

    testTask.usCpuAffiMask = 0x4;                    /* 无有效核位：拒绝 */
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);

    testTask.usCpuAffiMask = 0x1;                    /* 有效：允许 */
    ret = LOS_TaskCreate(&taskId, &testTask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    LOS_TaskDelete(taskId);
    return LOS_OK;
}

VOID ItSmpLosTaskCreateAffi01(void)
{
    TEST_ADD_CASE("ItSmpLosTaskCreateAffi01", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL1, TEST_FUNCTION);
}
#endif


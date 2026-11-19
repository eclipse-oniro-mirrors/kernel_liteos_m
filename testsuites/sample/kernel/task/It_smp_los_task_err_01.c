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
/* 用例简要描述: ItSmpLosTaskErr01 LOS_TaskCpuAffiSet 掩码为零返回错误码 */
#include "It_los_task.h"
#ifdef LOSCFG_KERNEL_SMP

static UINT32 Testcase(void)
{
    UINT32 ret;
    UINT32 taskId = LOS_CurTaskIDGet();

    ret = LOS_TaskCpuAffiSet(taskId, 0);
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

VOID ItSmpLosTaskErr01(void)
{
    TEST_ADD_CASE("ItSmpLosTaskErr01", Testcase, TEST_LOS, TEST_FN_KERNEL_TASKMGMT, TEST_LEVEL0, TEST_FUNCTION);
}
#endif


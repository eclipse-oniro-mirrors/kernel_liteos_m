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
/* 用例简要描述: ItSmpLosHwiErr01 LOS_HwiSendIpi SGI 号越界（>15）返回错误码 */
#include "It_smp_hwi.h"
#ifdef LOSCFG_KERNEL_SMP

static UINT32 Testcase(void)
{
    UINT32 ret = LOS_HwiSendIpi(16, LOSCFG_KERNEL_CPU_MASK);   /* SGI 0~15，16 越界 */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

VOID ItSmpLosHwiErr01(void)
{
    TEST_ADD_CASE("ItSmpLosHwiErr01", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL0, TEST_FUNCTION);
}
#endif


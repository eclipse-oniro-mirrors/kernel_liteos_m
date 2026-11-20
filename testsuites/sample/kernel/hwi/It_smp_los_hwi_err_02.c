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
/* 用例简要描述: ItSmpLosHwiErr02 LOS_HwiSetAffinity 掩码无效返回错误码 */
#include "It_smp_hwi.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static UINT32 Testcase(void)
{
    UINT32 ret;
    ret = LOS_HwiSetAffinity(HWI_NUM_TEST, 0);      /* 掩码 0 无效 */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSetAffinity(HWI_NUM_TEST, 0x8);    /* 超出双核 0x3 */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

VOID ItSmpLosHwiErr02(void)
{
#if KERNEL_BUG_20_DEPEND_1298
    TEST_ADD_CASE("ItSmpLosHwiErr02", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL0, TEST_FUNCTION);
#endif
}
#endif


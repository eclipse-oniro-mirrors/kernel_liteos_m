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
/* 用例简要描述: ItSmpLosHwiBound01 PPI/SGI 绑核被拒与 SPI 有效域验证（!1298 da35e51e） */
#include "It_smp_hwi.h"
#include "bug_isolate.h"
#ifdef LOSCFG_KERNEL_SMP

static UINT32 Testcase(void)
{
    UINT32 ret;
    UINT32 currMask = CPUID_TO_AFFI_MASK(ArchCurrCpuid());

    /* PPI 域（16~31）：ITARGETSR 对 PPI 无效——!1298 起拒绝 */
    ret = LOS_HwiSetAffinity(16, currMask);          /* PPI 16 */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSetAffinity(29, currMask);          /* 系统 tick PPI 29 */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSetAffinity(31, currMask);          /* PPI 31 */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    /* SGI 域（0~15）：同样拒绝 */
    ret = LOS_HwiSetAffinity(1, currMask);          /* LOS_MP_IPI_SCHEDULE */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    /* SPI 有效域下限/上限 */
    ret = LOS_HwiSetAffinity(32, currMask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSetAffinity(127, currMask);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

VOID ItSmpLosHwiBound01(void)
{
#if KERNEL_BUG_20_DEPEND_1298
    TEST_ADD_CASE("ItSmpLosHwiBound01", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
#endif


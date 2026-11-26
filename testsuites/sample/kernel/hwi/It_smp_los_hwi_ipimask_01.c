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
/* 用例简要描述: ItSmpLosHwiIpiMask01 LOS_HwiSendIpi cpuMask 边界校验（0/越界/有效） */
#include "It_smp_hwi.h"
#ifdef LOSCFG_KERNEL_SMP

static UINT32 Testcase(void)
{
    UINT32 ret;
    ret = LOS_HwiSendIpi(1, 0);                      /* mask=0：无目标核 */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSendIpi(1, 0x8);                    /* 越出双核 0x3 */
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSendIpi(1, 0x3);                     /* 有效：双核各得一份（D7） */
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSendIpi(1, CPUID_TO_AFFI_MASK((ArchCurrCpuid() + 1) % LOSCFG_KERNEL_CORE_NUM));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    return LOS_OK;
}

VOID ItSmpLosHwiIpiMask01(void)
{
    TEST_ADD_CASE("ItSmpLosHwiIpiMask01", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL1, TEST_FUNCTION);
}
#endif


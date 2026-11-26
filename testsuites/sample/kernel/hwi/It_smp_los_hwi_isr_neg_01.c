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
/* 用例简要描述: ItSmpLosHwiIsrNeg01 ISR 内调用 LOS_TaskCpuAffiSet 允许成功（开发定论：功能书 §7.1 该项标 ❌ 属文档错误） */
#include "It_smp_hwi.h"
#ifdef LOSCFG_KERNEL_SMP

static volatile UINT32 g_isrRet = 0;

static VOID HwiF01(UINT32 arg)
{
    TestHwiClear(HWI_NUM_TEST);
    g_isrRet = LOS_TaskCpuAffiSet(LOS_CurTaskIDGet(), CPUID_TO_AFFI_MASK(ArchCurrCpuid()));
}

static UINT32 Testcase(void)
{
    UINT32 ret;

    TestHwiClear(HWI_NUM_TEST);
    ret = LOS_HwiCreate(HWI_NUM_TEST, 1, 0, (HWI_PROC_FUNC)HwiF01, 0);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSetAffinity(HWI_NUM_TEST, CPUID_TO_AFFI_MASK(ArchCurrCpuid()));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    TestHwiTrigger(HWI_NUM_TEST);
    TestHwiDelete(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(g_isrRet, LOS_OK, g_isrRet);   /* ISR 内改亲和性：实现允许（不阻塞，无 guard 是设计而非缺陷） */
    return LOS_OK;
}

VOID ItSmpLosHwiIsrNeg01(void)
{
    TEST_ADD_CASE("ItSmpLosHwiIsrNeg01", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL1, TEST_FUNCTION);
}
#endif


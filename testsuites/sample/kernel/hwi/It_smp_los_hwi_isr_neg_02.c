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
/* 用例简要描述: ItSmpLosHwiIsrNeg02 ISR 内调用 LOS_MuxPost 返回禁止错误码 */
#include "It_smp_hwi.h"
#ifdef LOSCFG_KERNEL_SMP

static volatile UINT32 g_isrRet = 0;
static volatile UINT32 g_mutexId = 0;

static VOID HwiF02(UINT32 arg)
{
    TestHwiClear(HWI_NUM_TEST);
    g_isrRet = LOS_MuxPost(g_mutexId);
}

static UINT32 Testcase(void)
{
    UINT32 ret;

    ret = LOS_MuxCreate((UINT32 *)&g_mutexId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    TestHwiClear(HWI_NUM_TEST);
    ret = LOS_HwiCreate(HWI_NUM_TEST, 1, 0, (HWI_PROC_FUNC)HwiF02, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSetAffinity(HWI_NUM_TEST, CPUID_TO_AFFI_MASK(ArchCurrCpuid()));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    TestHwiTrigger(HWI_NUM_TEST);
    ICUNIT_ASSERT_NOT_EQUAL(g_isrRet, LOS_OK, g_isrRet);   /* 禁 ISR 释放互斥锁 */

    TestHwiDelete(HWI_NUM_TEST);
    (void)LOS_MuxDelete((UINT32)g_mutexId);
    return LOS_OK;
}

VOID ItSmpLosHwiIsrNeg02(void)
{
    TEST_ADD_CASE("ItSmpLosHwiIsrNeg02", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL1, TEST_FUNCTION);
}
#endif


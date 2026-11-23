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
/* 用例简要描述: ItSmpLosHwiIsrNeg03 ISR 内调用 LOS_EventRead 返回禁止错误码 */
#include "It_smp_hwi.h"
#ifdef LOSCFG_KERNEL_SMP

static volatile UINT32 g_isrRet = 0;
static volatile UINT32 g_eventId = 0;

static VOID HwiF03(UINT32 arg)
{
    TestHwiClear(HWI_NUM_TEST);
    g_isrRet = LOS_EventRead((EVENT_CB_S *)g_eventId, 0x1, LOS_WAITMODE_AND, 0);
}

static UINT32 Testcase(void)
{
    UINT32 ret;
    EVENT_CB_S eventId;

    (void)memset_s(&eventId, sizeof(eventId), 0, sizeof(eventId));
    ret = LOS_EventInit(&eventId);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    g_eventId = (UINT32)(uintptr_t)&eventId;

    TestHwiClear(HWI_NUM_TEST);
    ret = LOS_HwiCreate(HWI_NUM_TEST, 1, 0, (HWI_PROC_FUNC)HwiF03, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiSetAffinity(HWI_NUM_TEST, CPUID_TO_AFFI_MASK(ArchCurrCpuid()));
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    TestHwiTrigger(HWI_NUM_TEST);
    ICUNIT_ASSERT_NOT_EQUAL(g_isrRet, LOS_OK, g_isrRet);   /* 禁 ISR 读事件 */

    TestHwiDelete(HWI_NUM_TEST);
    (void)LOS_EventDestroy(&eventId);
    return LOS_OK;
}

VOID ItSmpLosHwiIsrNeg03(void)
{
    TEST_ADD_CASE("ItSmpLosHwiIsrNeg03", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL1, TEST_FUNCTION);
}
#endif


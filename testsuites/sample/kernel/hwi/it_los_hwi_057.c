/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "it_los_hwi.h"

#if defined(LOSCFG_ARM_GIC_V3) && defined(LOSCFG_MCS)

/* 用例简要描述: 锁定态 GICD 访问 fault 注入(T2/A1 板级专项)。
 * 必须为测试序列最后一个用例: 注入的 Translation Fault 经 GICD-locked
 * 分类器打印专属提示后走既有异常流程, AMP slave 按 ExcSlavePoweroff
 * 自陷 CPU_OFF, 测试框架随之终止(设计行为)。通过证据 = 串口出现
 * "GICD access blocked" 提示 + 异常 dump, 不以 iCunit 计数回报。 */
static UINT32 Testcase(VOID)
{
    volatile UINT32 *gicdCtl = (volatile UINT32 *)(UINTPTR)GICD_BASE;
    UINT32 ret;
    UINT32 val;

    ret = LOS_GicdMmuDisable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), TRUE, FALSE, EXIT);

    /* fault 注入: 锁定态读 GICD_CTLR -> Translation Fault(ESR.EC=0x25)
     * -> los_fault.c GICD 分类器专属提示 -> 异常 dump -> CPU_OFF。 */
    val = *gicdCtl;
    (VOID)val; /* 不可达: fault 不返回 */

    return LOS_OK; /* 不可达 */
EXIT:
    (VOID)LOS_GicdMmuEnable();
    return LOS_OK;
}

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi057
 * @par TestCase_TestCase_Type
 * Function test (fault injection, sequence-terminal)
 * @brief Inject a GICD read while the window is locked (T2/A1 proof)
 * @par TestCase_Pretreatment_Condition
 * RK3568 AMP slave; MUST be the last case of the whole suite.
 * @par TestCase_Test_Steps
 * step1: lock the window (Disable, assert IsLocked).
 * step2: read GICD_CTLR -> hardware Translation Fault.
 * step3: (serial) GICD-locked classifier hint + exception dump + CPU_OFF.
 * @par TestCase_Expected_Result
 * 1.Serial shows "GICD access blocked: FAR=... locked by
 *   LOS_GicdMmuDisable()" before the generic exception dump.
 * 2.Slave powers itself off (ExcSlavePoweroff); framework terminates.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * false (evidence is serial output, not iCunit accounting)
 */
VOID ItLosHwi057(VOID)
{
#if defined(LOSCFG_ARM_GIC_V3) && defined(LOSCFG_MCS)
    TEST_ADD_CASE("ItLosHwi057", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL1, TEST_FUNCTION);
#endif
}
#endif /* LOSCFG_ARM_GIC_V3 && LOSCFG_MCS */

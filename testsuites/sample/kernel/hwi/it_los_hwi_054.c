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


/* 循环次数: 自动化用例内取 100 控制时长(全量套件场景);
 * 1000 次压力循环为上板专项(T6 全量口径)。 */
#define HWI_GICD_MMU_LOOP 100

/* 用例简要描述: GICD MMU 开关状态机往返/ALREADY 严格语义/压力循环(T3/T4/T6/D6) */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 i;

    /* F-Pos: 启动默认开放(D2) */
    ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), FALSE, TRUE, EXIT);

    /* F-Pos: 上锁/解锁往返 + 状态镜像翻转(T4) */
    ret = LOS_GicdMmuDisable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), TRUE, FALSE, EXIT);
    ret = LOS_GicdMmuEnable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), FALSE, TRUE, EXIT);

    /* F-Pos/F-Inv: ALREADY 严格语义(D6): 重复调用返回 ALREADY 码且状态不变,
     * 状态认知偏差必须暴露(以为锁着实际开着 = 防护失效无人知晓) */
    ret = LOS_GicdMmuEnable();
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_HWI_GICD_ALREADY_OPEN, ret, EXIT);
    ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), FALSE, TRUE, EXIT);
    ret = LOS_GicdMmuDisable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_GicdMmuDisable();
    ICUNIT_GOTO_EQUAL(ret, LOS_ERRNO_HWI_GICD_ALREADY_LOCKED, ret, EXIT);
    ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), TRUE, FALSE, EXIT);
    ret = LOS_GicdMmuEnable();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Loop: 压力往返(T6 缩配), 每轮校验状态翻转与返回值 */
    for (i = 0; i < HWI_GICD_MMU_LOOP; i++) {
        ret = LOS_GicdMmuDisable();
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), TRUE, FALSE, EXIT);
        ret = LOS_GicdMmuEnable();
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        ICUNIT_GOTO_EQUAL(LOS_GicdMmuIsLocked(), FALSE, TRUE, EXIT);
    }

    return LOS_OK;
EXIT:
    (VOID)LOS_GicdMmuEnable(); /* 现场恢复: 终态保持开放 */
    return LOS_OK;
}

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi054
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test LOS_GicdMmuDisable/Enable/IsLocked state round-trip and idempotency
 * @par TestCase_Pretreatment_Condition
 * RK3568 AMP slave (aarch64 GICv3 + MCS), GICD-quiet time.
 * @par TestCase_Test_Steps
 * step1: assert default open; disable->locked, enable->open.
 * step2: repeated disable/enable return the ALREADY error codes
        (LOS_ERRNO_HWI_GICD_ALREADY_LOCKED/_OPEN) with state unchanged.
 * step3: 100 lock/unlock cycles, verify return and mirror each round.
 * @par TestCase_Expected_Result
 * 1.All calls return LOS_OK; IsLocked mirrors the page-table state.
 * 2.No page-table pool growth (see ItLosHwi056).
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 */
VOID ItLosHwi054(VOID)
{
    /* 状态机往返/幂等/压力循环 */
    TEST_ADD_CASE("ItLosHwi054", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL1, TEST_FUNCTION);
}

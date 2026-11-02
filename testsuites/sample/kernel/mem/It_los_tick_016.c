/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the conditions of the LOSCFG-style open source license.
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY
 * OR FIT FOR A PARTICULAR PURPOSE.
 */

#include "It_los_mem.h"
#include "los_tick.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* 用例简要描述: OsCpuTick2MS / OsCpuTick2US(los_tick.h 对外声明)直测。
 * 1) F-Inv: cpuTick/msHi/msLo 任一为 NULL → LOS_ERRNO_SYS_PTR_NULL(两接口全排列);
 * 2) F-Pos: cntHi=0, cntLo = N * LOS_CyclePerTickGet()(N 个 tick 的周期数),
 *    换算毫秒 = N * (1000 / LOSCFG_BASE_CORE_TICK_PER_SECOND), 微秒为其 1000 倍,
 *    用区间断言吸收整数截断误差; hi=0 断言;
 * 3) F-Pos: cntHi!=0 的 64 位输入, 输出 hi 位非零(大周期数换算)。
 * 覆盖目标: los_tick.c OsCpuTick2MS/OsCpuTick2US 全分支(NULL 拒绝 + 正常换算)。 */

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 hi = 0;
    UINT32 lo = 0;
    UINT32 cyclePerTick;
    UINT64 expectMs;
    UINT64 expectUs;
    UINT64 actualMs;
    UINT64 actualUs;
    CpuTick cpuTick;

    cyclePerTick = LOS_CyclePerTickGet();
    ICUNIT_ASSERT_NOT_EQUAL(cyclePerTick, 0, cyclePerTick);

    /* F-Inv: OsCpuTick2MS 参数 NULL 全排列 */
    cpuTick.cntHi = 0;
    cpuTick.cntLo = cyclePerTick;
    ret = OsCpuTick2MS(NULL, &hi, &lo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_PTR_NULL, ret);
    ret = OsCpuTick2MS(&cpuTick, NULL, &lo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_PTR_NULL, ret);
    ret = OsCpuTick2MS(&cpuTick, &hi, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_PTR_NULL, ret);

    /* F-Inv: OsCpuTick2US 参数 NULL 全排列 */
    ret = OsCpuTick2US(NULL, &hi, &lo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_PTR_NULL, ret);
    ret = OsCpuTick2US(&cpuTick, NULL, &lo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_PTR_NULL, ret);
    ret = OsCpuTick2US(&cpuTick, &hi, NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_SYS_PTR_NULL, ret);

    /* F-Pos: 10 个 tick 的周期数换算——10 tick 对应 10 * (1000/TICK_PER_SECOND) ms */
    cpuTick.cntHi = 0;
    cpuTick.cntLo = 10 * cyclePerTick; /* 10 ticks */
    hi = 0;
    lo = 0;
    ret = OsCpuTick2MS(&cpuTick, &hi, &lo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(hi, 0, hi);
    expectMs = ((UINT64)10 * OS_SYS_MS_PER_SECOND) / LOSCFG_BASE_CORE_TICK_PER_SECOND;
    actualMs = lo;
    /* 容忍浮点/截断 1ms 级误差 */
    ICUNIT_ASSERT_WITHIN_EQUAL(actualMs, expectMs - 1, expectMs + 1, actualMs);

    hi = 0;
    lo = 0;
    ret = OsCpuTick2US(&cpuTick, &hi, &lo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(hi, 0, hi);
    expectUs = expectMs * 1000; /* us per ms */
    actualUs = lo;
    ICUNIT_ASSERT_WITHIN_EQUAL(actualUs, expectUs - 1500, expectUs + 1500, actualUs);

    /* F-Pos: cntHi != 0 的大周期输入, 毫秒输出进位到 hi(选足够大的周期数:
     * cntHi=2 → 2^33 周期, g_sysClock ~ MHz 级 → 换算 ms 必然 >= 2^32, hi != 0) */
    cpuTick.cntHi = 0x40000000; /* 2^63 cycles, 任一 g_sysClock 下换算 ms/us 必然 >= 2^32 */
    cpuTick.cntLo = 0;
    hi = 0;
    lo = 0;
    ret = OsCpuTick2MS(&cpuTick, &hi, &lo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_NOT_EQUAL(hi, 0, hi);

    hi = 0;
    lo = 0;
    ret = OsCpuTick2US(&cpuTick, &hi, &lo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_NOT_EQUAL(hi, 0, hi);

    return LOS_OK;
}

/**
 * @ingroup TEST_MEM
 * @par TestCase_Number
 * ItLosTick016
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface OsCpuTick2MS/OsCpuTick2US
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: call with NULL in each parameter position (both APIs).
 * step2: convert 10-tick cycles and check ms/us results within tolerance.
 * step3: convert 2^33 cycles and check 64-bit output carry.
 * @par TestCase_Expected_Result
 * 1.NULL parameters rejected with LOS_ERRNO_SYS_PTR_NULL.
 * 2.Conversion results match tick-based expectations.
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosTick016(VOID)
{
    TEST_ADD_CASE("ItLosTick016", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

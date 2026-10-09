/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: DCache prefetch init validates both the ICL and SCL operands
 * independently (either out of range -> failure).
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    ICUNIT_ASSERT_EQUAL(ArchDCachePrefetchEnable(CACHE_PREF_1_LINES, CACHE_PREF_1_LINES), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchDCachePrefetchEnable(CACHE_PREF_8_LINES, CACHE_PREF_8_LINES), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_NOT_EQUAL(ArchDCachePrefetchEnable(CACHE_PREF_LINES_MAX, CACHE_PREF_4_LINES), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_NOT_EQUAL(ArchDCachePrefetchEnable(CACHE_PREF_4_LINES, CACHE_PREF_LINES_MAX), LOS_OK, LOS_OK);
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache004
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchDCachePrefetchEnable dual-operand validation
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: call ArchDCachePrefetchEnable with both operands valid.
 * step2: call with only iclValue out of range, then only sclValue out of range.
 * step3: verify each out-of-range operand independently causes failure.
 * @par TestCase_Expected_Result
 * 1.ArchDCachePrefetchEnable returns LOS_OK only when both iclValue and
 *   sclValue are within 1..8 lines.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache004(VOID)
{
    TEST_ADD_CASE("ItLosCache004", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

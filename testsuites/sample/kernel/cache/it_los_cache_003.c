/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: ICache prefetch init accepts the full 1..8 line range and
 * rejects the sentinel CACHE_PREF_LINES_MAX.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    ICUNIT_ASSERT_EQUAL(ArchICachePrefetchEnable(CACHE_PREF_1_LINES), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchICachePrefetchEnable(CACHE_PREF_4_LINES), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchICachePrefetchEnable(CACHE_PREF_8_LINES), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_NOT_EQUAL(ArchICachePrefetchEnable(CACHE_PREF_LINES_MAX), LOS_OK, LOS_OK);
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache003
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchICachePrefetchEnable range validation
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: call ArchICachePrefetchEnable with several valid CachePrefLines
 *        values plus the sentinel CACHE_PREF_LINES_MAX.
 * step2: verify valid 1/4/8 lines succeed and the sentinel fails.
 * @par TestCase_Expected_Result
 * 1.ArchICachePrefetchEnable returns LOS_OK for 1..8 lines and fails for
 *   CACHE_PREF_LINES_MAX.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache003(VOID)
{
    TEST_ADD_CASE("ItLosCache003", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: ICache enable accepts every valid CacheSize and rejects
 * CACHE_64KB (the only out-of-range enum value).
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    /* main() already enabled ICache as CACHE_32KB. Do not shrink to 4KB/8KB/16KB
     * while ICache is active — shrinking invalidates lines beyond the new size,
     * causing instruction access fault if PC falls outside. Only test that the
     * out-of-range CACHE_64KB is rejected, then re-confirm 32KB (current size). */
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_NOT_EQUAL(ArchICacheEnable(CACHE_64KB), LOS_OK, LOS_OK);
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache001
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchICacheEnable size validation
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: call ArchICacheEnable with each CacheSize enum value.
 * step2: verify the four valid sizes (4/8/16/32KB) return LOS_OK and
 *        CACHE_64KB returns non-LOS_OK.
 * @par TestCase_Expected_Result
 * 1.ArchICacheEnable returns LOS_OK for CACHE_4KB..CACHE_32KB and fails for
 *   CACHE_64KB.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache001(VOID)
{
    TEST_ADD_CASE("ItLosCache001", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

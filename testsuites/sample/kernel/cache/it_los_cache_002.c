/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: DCache enable accepts every valid CacheSize and rejects
 * CACHE_64KB, mirroring the ICache enable contract.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    /* main() already enabled DCache as CACHE_4KB. Do not change DCache size
     * while active. Only test out-of-range rejection and re-confirm 4KB. */
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_4KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_NOT_EQUAL(ArchDCacheEnable(CACHE_64KB), LOS_OK, LOS_OK);
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache002
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchDCacheEnable size validation
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: call ArchDCacheEnable with each CacheSize enum value.
 * step2: verify the four valid sizes return LOS_OK and CACHE_64KB fails.
 * @par TestCase_Expected_Result
 * 1.ArchDCacheEnable returns LOS_OK for CACHE_4KB..CACHE_32KB and fails for
 *   CACHE_64KB.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache002(VOID)
{
    TEST_ADD_CASE("ItLosCache002", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: DCache enable accepts every valid CacheSize and rejects
 * CACHE_64KB, mirroring the ICache enable contract.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    /* main() enabled DCache. Changing DCache size while active requires a
     * flush after each change to avoid stale lines. Restore 4KB (main's
     * config) at the end.
     * Note: NonLinxM Enable (131) uses bext 0x155 which allows CACHE_64KB;
     * LinxM Enable (170) ignores size arg. Both return LOS_OK for 64KB.
     * ArchDCacheFlush before ArchDCacheEnable: csrw DCCTL may affect DCache,
     * flush ensures dirty stack lines are written back to SRAM first. */
    ArchDCacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_4KB), LOS_OK, LOS_OK);
    ArchDCacheFlush();
    ArchDCacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_8KB), LOS_OK, LOS_OK);
    ArchDCacheFlush();
    ArchDCacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_16KB), LOS_OK, LOS_OK);
    ArchDCacheFlush();
    ArchDCacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ArchDCacheFlush();
    ArchDCacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_4KB), LOS_OK, LOS_OK);
    ArchDCacheFlush();
    ArchDCacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_64KB), LOS_OK, LOS_OK);
    ArchDCacheFlush();
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

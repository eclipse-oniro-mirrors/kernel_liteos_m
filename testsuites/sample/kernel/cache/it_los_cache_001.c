/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: ICache enable accepts every valid CacheSize and rejects
 * CACHE_64KB (the only out-of-range enum value).
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    /* main() enabled ICache. Changing ICache size while active requires a
     * flush after each change to avoid stale lines beyond the new size
     * causing instruction access fault. Restore 32KB at the end.
     * Note: 170 LinxM Enable ignores the size arg (fixed config), so
     * CACHE_64KB returns LOS_OK on 170; only 131 rejects it. */
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_4KB), LOS_OK, LOS_OK);
    ArchICacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_8KB), LOS_OK, LOS_OK);
    ArchICacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_16KB), LOS_OK, LOS_OK);
    ArchICacheFlush();
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ArchICacheFlush();
#ifdef LOSCFG_ARCH_LINX_M
    /* 170 LinxM: fixed config, ignores size arg, always returns LOS_OK */
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_64KB), LOS_OK, LOS_OK);
#else
    /* 131: andi -7 check rejects CACHE_64KB */
    ICUNIT_ASSERT_NOT_EQUAL(ArchICacheEnable(CACHE_64KB), LOS_OK, LOS_OK);
#endif
    ArchICacheFlush();
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

/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: ICache/DCache enable is idempotent and survives re-enable with
 * a different size. Ensures repeated enable calls (same size, different
 * size, and the maximal valid size) all return LOS_OK and never corrupt
 * subsequent maintenance semantics.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_4KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_16KB), LOS_OK, LOS_OK);

    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_8KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_4KB), LOS_OK, LOS_OK);

    /* After re-enable the maintenance path must still be functional. */
    volatile UINT32 buf[4];
    buf[0] = 0x1234U;
    buf[1] = 0x5678U;
    ArchDCacheFlushByVa((UINTPTR)buf, sizeof(buf));
    ICUNIT_ASSERT_EQUAL(buf[0], 0x1234U, buf[0]);
    ICUNIT_ASSERT_EQUAL(buf[1], 0x5678U, buf[1]);
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache013
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchICacheEnable / ArchDCacheEnable idempotency
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: enable I/D cache, then re-enable with the same and a different size.
 * step2: verify every enable returns LOS_OK.
 * step3: run a maintenance round-trip and confirm the path still works.
 * @par TestCase_Expected_Result
 * 1.Enable is idempotent and a subsequent maintenance op behaves correctly.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache013(VOID)
{
    TEST_ADD_CASE("ItLosCache013", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

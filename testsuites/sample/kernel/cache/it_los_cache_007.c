/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: ArchDCacheInvByVa must preserve cache-line boundary data: it
 * writes back partial head/tail lines before invalidating, so a buffer
 * written then invalidated is still readable.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    static volatile UINT32 buf[24];
    UINT32 i;

    for (i = 0; i < 24; i++) {
        buf[i] = 0x1000U + i;
    }
    ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
    ArchDCacheInvByVa((UINTPTR)buf, sizeof(buf));
    for (i = 0; i < 24; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x1000U + i, buf[i]);
    }
    ArchDCacheInvByVa((UINTPTR)buf, 0U);
    for (i = 0; i < 24; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x1000U + i, buf[i]);
    }
    ArchDCacheInvByVa((UINTPTR)buf + 3U, 47U);
    for (i = 0; i < 24; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x1000U + i, buf[i]);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache007
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchDCacheInvByVa boundary-writeback semantics
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: clean then invalidate a fully-aligned buffer; verify data survives.
 * step2: invalidate a zero-size range (no-op).
 * step3: invalidate an unaligned sub-range (head+tail partial lines); verify
 *        the surrounding data survives the writeback-then-invalidate.
 * @par TestCase_Expected_Result
 * 1.ArchDCacheInvByVa never loses clean data at unaligned boundaries.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache007(VOID)
{
    TEST_ADD_CASE("ItLosCache007", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

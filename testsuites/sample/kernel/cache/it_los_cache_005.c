/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: full-cache flush/invalidate complete without fault and keep a
 * stack-backed buffer intact (write-back semantics: cleaned/flushed dirty
 * data must remain observable in memory).
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    volatile UINT32 buf[8];
    UINT32 i;

    for (i = 0; i < 8; i++) {
        buf[i] = (0xA5A50000U | i);
    }
    ArchDCacheFlush();
    for (i = 0; i < 8; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0xA5A50000U | i), buf[i]);
    }
    ArchICacheFlush();
    ArchDCacheInvalidate();
    for (i = 0; i < 8; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0xA5A50000U | i), buf[i]);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache005
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchICacheFlush / ArchDCacheFlush / ArchDCacheInvalidate
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: write a known pattern into a stack buffer.
 * step2: ArchDCacheFlush then verify the pattern survives (write-back).
 * step3: ArchICacheFlush + ArchDCacheInvalidate then verify the pattern still
 *        survives (full invalidate of a clean line must not lose data).
 * @par TestCase_Expected_Result
 * 1.The buffer contents are unchanged after each full-cache operation.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache005(VOID)
{
    TEST_ADD_CASE("ItLosCache005", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

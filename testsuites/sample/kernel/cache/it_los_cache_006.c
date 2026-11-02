/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: ArchDCacheFlushByVa / ArchDCacheCleanByVa operate on aligned and
 * unaligned bases, including a zero-size range (must be a no-op, no fault)
 * and a sub-line unaligned range.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    volatile UINT32 buf[16];
    UINT32 i;

    for (i = 0; i < 16; i++) {
        buf[i] = i * 7U + 1U;
    }
    ArchDCacheFlushByVa((UINTPTR)buf, 0U);
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], i * 7U + 1U, buf[i]);
    }
    ArchDCacheFlushByVa((UINTPTR)buf, sizeof(buf));
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], i * 7U + 1U, buf[i]);
    }
    ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], i * 7U + 1U, buf[i]);
    }
    ArchDCacheFlushByVa((UINTPTR)buf + 1U, sizeof(buf) - 2U);
    ArchDCacheCleanByVa((UINTPTR)buf + 5U, 11U);
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], i * 7U + 1U, buf[i]);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache006
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchDCacheFlushByVa / ArchDCacheCleanByVa
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: fill a buffer with a known pattern.
 * step2: flush/clean with size 0 (no-op), full size, then unaligned base and
 *        sub-line sizes.
 * step3: verify the pattern is unchanged after each operation.
 * @par TestCase_Expected_Result
 * 1.None of the by-VA operations fault or corrupt the buffer; data survives.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache006(VOID)
{
    TEST_ADD_CASE("ItLosCache006", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

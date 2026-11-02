/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: cache-line boundary precision. Exercises ranges that are
 * exactly one line, exactly two lines, and exact multiples of
 * CACHE_LINE_SIZE so the per-line maintenance loop hits the loop-edge
 * (first/last/only line) paths deterministically. Verifies no fault and
 * data integrity for flush/clean/invalidate at every boundary shape.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    /* 8 lines = 256 bytes, comfortably larger than a single line. */
    static volatile UINT32 buf[(CACHE_LINE_SIZE * 8) / sizeof(UINT32)];
    UINT32 n = sizeof(buf) / sizeof(buf[0]);
    UINT32 i;

    for (i = 0; i < n; i++) {
        buf[i] = 0xB0B00000U | i;
    }

    /* Exactly one line at the start. */
    ArchDCacheFlushByVa((UINTPTR)buf, CACHE_LINE_SIZE);
    ArchDCacheCleanByVa((UINTPTR)buf, CACHE_LINE_SIZE);
    ArchDCacheInvByVa((UINTPTR)buf, CACHE_LINE_SIZE);
    for (i = 0; i < n; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0xB0B00000U | i, buf[i]);
    }

    /* Exactly two consecutive lines. */
    ArchDCacheFlushByVa((UINTPTR)buf, CACHE_LINE_SIZE * 2);
    ArchDCacheCleanByVa((UINTPTR)buf, CACHE_LINE_SIZE * 2);
    ArchDCacheInvByVa((UINTPTR)buf, CACHE_LINE_SIZE * 2);
    for (i = 0; i < n; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0xB0B00000U | i, buf[i]);
    }

    /* Whole buffer: exact multiple of CACHE_LINE_SIZE. */
    ArchDCacheFlushByVa((UINTPTR)buf, sizeof(buf));
    ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
    ArchDCacheInvByVa((UINTPTR)buf, sizeof(buf));
    for (i = 0; i < n; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0xB0B00000U | i, buf[i]);
    }

    /* One line mid-buffer (line-aligned interior range). */
    ArchDCacheFlushByVa((UINTPTR)buf + CACHE_LINE_SIZE * 3, CACHE_LINE_SIZE);
    ArchDCacheCleanByVa((UINTPTR)buf + CACHE_LINE_SIZE * 4, CACHE_LINE_SIZE);
    ArchDCacheInvByVa((UINTPTR)buf + CACHE_LINE_SIZE * 5, CACHE_LINE_SIZE);
    for (i = 0; i < n; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0xB0B00000U | i, buf[i]);
    }

    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache014
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test by-VA maintenance at exact cache-line boundaries
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: drive Flush/Clean/InvByVa with exactly one line, two lines, a full
 *        exact-multiple buffer, and an interior single line.
 * step2: verify the buffer is unchanged after each operation.
 * @par TestCase_Expected_Result
 * 1.Each exact-boundary range is handled without fault or data loss.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache014(VOID)
{
    TEST_ADD_CASE("ItLosCache014", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

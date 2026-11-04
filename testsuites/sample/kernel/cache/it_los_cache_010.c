/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: stress the by-VA path with a large range and many iterations,
 * covering cache-line stride access. Verifies stability (no fault) and data
 * integrity across repeated flush/clean/invalidate cycles.
 */

#include "it_los_cache.h"

#define CACHE_TEST_LOOPS 200

static UINT32 TestCase(VOID)
{
    static volatile UINT32 buf[256];
    UINT32 i;
    UINT32 loop;

    for (loop = 0; loop < CACHE_TEST_LOOPS; loop++) {
        for (i = 0; i < 256; i++) {
            buf[i] = loop * 256U + i;
        }
        ArchDCacheFlushByVa((UINTPTR)buf, sizeof(buf));
        ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
        ArchDCacheInvByVa((UINTPTR)buf, sizeof(buf));
        for (i = 0; i < 256; i++) {
            ICUNIT_ASSERT_EQUAL(buf[i], loop * 256U + i, buf[i]);
        }
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache010
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test by-VA flush/clean/invalidate stability over a large range
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: loop CACHE_TEST_LOOPS times over a 1KB buffer.
 * step2: each iteration writes, flushes, cleans and invalidates the whole
 *        buffer, then verifies every word.
 * @par TestCase_Expected_Result
 * 1.No fault occurs across all iterations and the buffer matches the written
 *   pattern after every cycle.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache010(VOID)
{
    TEST_ADD_CASE("ItLosCache010", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

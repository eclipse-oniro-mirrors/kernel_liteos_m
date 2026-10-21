/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: combined enable + maintenance scenario. Enables I/D cache,
 * exercises prefetch init, runs a write/flush/read round-trip, then invokes
 * every public ByVa/ByAddr/whole-cache entry to confirm the full interface
 * is wired and reachable through the closed archive.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    volatile UINT32 buf[32];
    UINT32 i;

    ICUNIT_ASSERT_EQUAL(ArchICacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchDCacheEnable(CACHE_32KB), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchICachePrefetchEnable(CACHE_PREF_4_LINES), LOS_OK, LOS_OK);
    ICUNIT_ASSERT_EQUAL(ArchDCachePrefetchEnable(CACHE_PREF_4_LINES, CACHE_PREF_4_LINES), LOS_OK, LOS_OK);

    for (i = 0; i < 32; i++) {
        buf[i] = (0x30000U | i);
    }
    ArchDCacheFlushByVa((UINTPTR)buf, sizeof(buf));
    ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
    ArchDCacheInvByVa((UINTPTR)buf, sizeof(buf));
    for (i = 0; i < 32; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0x30000U | i), buf[i]);
    }

    ArchICacheFlushByVa((UINTPTR)buf, sizeof(buf));
    ArchICacheFlushByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
    ArchDCacheFlushByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
    ArchDCacheCleanByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
    ArchDCacheInvByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));

    ArchICacheFlush();
    ArchDCacheFlush();
    ArchDCacheInvalidate();

    for (i = 0; i < 32; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0x30000U | i), buf[i]);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache011
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface coverage: every public ArchCache entry is reachable
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: enable I/D cache and prefetch for both caches.
 * step2: write a buffer and exercise flush/clean/invalidate by-VA, by-addr and
 *        whole-cache forms in sequence.
 * step3: verify the buffer survives the full maintenance sequence.
 * @par TestCase_Expected_Result
 * 1.Every public cache API returns/completes and data integrity is preserved.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache011(VOID)
{
    TEST_ADD_CASE("ItLosCache011", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

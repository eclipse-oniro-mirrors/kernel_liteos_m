/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: the ByAddr wrappers (start/end form) align both endpoints and
 * delegate to the ByVa form. Verify they accept equal start==end (zero
 * span), aligned spans, and unaligned spans without corrupting memory.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    static volatile UINT32 buf[20];
    UINT32 i;

    for (i = 0; i < 20; i++) {
        buf[i] = 0x55000000U | i;
    }
    ArchICacheFlushByAddr((UINTPTR)buf, (UINTPTR)buf);
    ArchDCacheFlushByAddr((UINTPTR)buf, (UINTPTR)buf);
    ArchDCacheCleanByAddr((UINTPTR)buf, (UINTPTR)buf);
    ArchDCacheInvByAddr((UINTPTR)buf, (UINTPTR)buf);
    for (i = 0; i < 20; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0x55000000U | i), buf[i]);
    }
    ArchDCacheFlushByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
    ArchDCacheCleanByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
    for (i = 0; i < 20; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0x55000000U | i), buf[i]);
    }
    ArchDCacheFlushByAddr((UINTPTR)buf + 3U, (UINTPTR)buf + sizeof(buf) - 5U);
    ArchDCacheCleanByAddr((UINTPTR)buf + 3U, (UINTPTR)buf + sizeof(buf) - 5U);
    ArchDCacheInvByAddr((UINTPTR)buf + 3U, (UINTPTR)buf + sizeof(buf) - 5U);
    for (i = 0; i < 20; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0x55000000U | i), buf[i]);
    }
    ArchICacheFlushByAddr((UINTPTR)buf + 2U, (UINTPTR)buf + sizeof(buf) - 2U);
    for (i = 0; i < 20; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0x55000000U | i), buf[i]);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache009
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchICacheFlushByAddr / ArchDCacheFlushByAddr /
 *       ArchDCacheCleanByAddr / ArchDCacheInvByAddr
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: zero-span calls (start == end) for all four ByAddr APIs.
 * step2: aligned full-buffer span for flush + clean.
 * step3: unaligned sub-buffer span for flush + clean + invalidate + icache.
 * step4: verify the buffer is unchanged after every call.
 * @par TestCase_Expected_Result
 * 1.The ByAddr wrappers handle zero-span, aligned and unaligned spans without
 *   fault or data corruption.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache009(VOID)
{
    TEST_ADD_CASE("ItLosCache009", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

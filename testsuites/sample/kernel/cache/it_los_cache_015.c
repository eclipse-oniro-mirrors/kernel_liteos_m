/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: flush vs clean vs invalidate data semantics. Confirms the
 * write-back contract: flush (clean+invalidate) and clean preserve dirty
 * data so a post-maintenance read still observes the written value, and
 * that the boundary-writeback in ArchDCacheInvByVa keeps shared-line
 * neighbours intact even when only a sub-range is invalidated.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    static volatile UINT32 buf[16];
    UINT32 i;

    /* --- flush preserves written data (write-back + invalidate) --- */
    for (i = 0; i < 16; i++) {
        buf[i] = 0xF1A50000U | i;
    }
    ArchDCacheFlushByVa((UINTPTR)buf, sizeof(buf));
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0xF1A50000U | i, buf[i]);
    }

    /* --- clean preserves written data (write-back only, no invalidate) --- */
    for (i = 0; i < 16; i++) {
        buf[i] = 0xC1EA0000U | i;
    }
    ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0xC1EA0000U | i, buf[i]);
    }

    /* --- invalidate mid-buffer sub-range must not clobber neighbours --- */
    for (i = 0; i < 16; i++) {
        buf[i] = 0x1BAD0000U | i;
    }
    ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
    /* Invalidate an unaligned interior window; head/tail partial lines
     * are cleaned first so the surrounding words survive. */
    ArchDCacheInvByVa((UINTPTR)&buf[3], 9 * sizeof(UINT32));
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x1BAD0000U | i, buf[i]);
    }

    /* --- whole-cache flush + invalidate keep a clean buffer intact --- */
    for (i = 0; i < 16; i++) {
        buf[i] = 0x600D0000U | i;
    }
    ArchDCacheFlush();
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x600D0000U | i, buf[i]);
    }
    ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
    ArchDCacheInvalidate();
    for (i = 0; i < 16; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x600D0000U | i, buf[i]);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache015
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test flush/clean/invalidate write-back data semantics
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: write a pattern and flush; verify the pattern survives.
 * step2: write a new pattern and clean; verify the pattern survives.
 * step3: clean, then invalidate an unaligned interior sub-range; verify the
 *        surrounding neighbour words survive (boundary writeback).
 * step4: whole-cache flush then whole-cache invalidate of a clean buffer;
 *        verify data survives.
 * @par TestCase_Expected_Result
 * 1.Flush and clean preserve dirty data; invalidate of a clean sub-range
 *   preserves shared-line neighbours.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache015(VOID)
{
    TEST_ADD_CASE("ItLosCache015", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

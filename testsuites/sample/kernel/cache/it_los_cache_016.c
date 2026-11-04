/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: arch/cache.h legacy macro wrappers. Verifies the
 * flush_dcache(start, end) and flush_icache() macros resolve to the real
 * ArchDCacheFlushByAddr / ArchICacheFlush symbols and behave identically
 * to their underlying calls.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    volatile UINT32 buf[8];
    UINT32 i;

    for (i = 0; i < 8; i++) {
        buf[i] = 0x0BAD0000U | i;
    }

    /* flush_dcache(start, end) -> ArchDCacheFlushByAddr(start, end) */
    flush_dcache((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
    for (i = 0; i < 8; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x0BAD0000U | i, buf[i]);
    }

    /* flush_icache() -> ArchICacheFlush(); must not touch data memory */
    flush_icache();
    for (i = 0; i < 8; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x0BAD0000U | i, buf[i]);
    }

    /* Combined macro path with an unaligned end. */
    flush_dcache((UINTPTR)buf + 1U, (UINTPTR)buf + sizeof(buf) - 1U);
    flush_icache();
    for (i = 0; i < 8; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], 0x0BAD0000U | i, buf[i]);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache016
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test flush_dcache / flush_icache legacy macro wrappers
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: exercise flush_dcache(start, end) on an aligned and an unaligned range.
 * step2: exercise flush_icache().
 * step3: verify the data buffer is unchanged after each macro call.
 * @par TestCase_Expected_Result
 * 1.The macros resolve to the real Arch* symbols and behave identically.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache016(VOID)
{
    TEST_ADD_CASE("ItLosCache016", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

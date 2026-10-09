/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: ArchICacheFlushByVa covers aligned, zero-size and unaligned
 * ranges without fault. ICache flush does not dirty data memory, so a
 * written buffer must read back identically.
 */

#include "it_los_cache.h"

static UINT32 TestCase(VOID)
{
    volatile UINT32 buf[12];
    UINT32 i;

    for (i = 0; i < 12; i++) {
        buf[i] = 0xDEAD0000U | i;
    }
    ArchICacheFlushByVa((UINTPTR)buf, 0U);
    for (i = 0; i < 12; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0xDEAD0000U | i), buf[i]);
    }
    ArchICacheFlushByVa((UINTPTR)buf, sizeof(buf));
    for (i = 0; i < 12; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0xDEAD0000U | i), buf[i]);
    }
    ArchICacheFlushByVa((UINTPTR)buf + 1U, sizeof(buf) - 3U);
    for (i = 0; i < 12; i++) {
        ICUNIT_ASSERT_EQUAL(buf[i], (0xDEAD0000U | i), buf[i]);
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache008
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface ArchICacheFlushByVa
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: write a known pattern to a stack buffer.
 * step2: ArchICacheFlushByVa with size 0, full size, then an unaligned
 *        sub-range.
 * step3: verify the data buffer is unchanged after each call.
 * @par TestCase_Expected_Result
 * 1.ArchICacheFlushByVa does not fault and does not corrupt data memory.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache008(VOID)
{
    TEST_ADD_CASE("ItLosCache008", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

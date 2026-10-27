/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * TestCase: interrupt-safety regression for the source cache implementation.
 * The closed liblos_cache.a had a layout-sensitive deadlock because its
 * multi-step maintenance sequences (csrw <INCVA> + csrw <MAINT> + fence,
 * iterated per line) ran without interrupt protection. The source
 * implementation in arch/risc-v/src/los_cache.c wraps every maintenance
 * sequence in LOS_IntLock / LOS_IntRestore. This case loops every public
 * maintenance entry many times to confirm the fix holds.
 */

#include "it_los_cache.h"

#define CACHE_INTSAFE_LOOPS 500

static UINT32 TestCase(VOID)
{
    static volatile UINT32 buf[32];
    UINT32 i;
    UINT32 loop;

    (VOID)ArchICacheEnable(CACHE_32KB);
    (VOID)ArchDCacheEnable(CACHE_32KB);

    for (loop = 0; loop < CACHE_INTSAFE_LOOPS; loop++) {
        for (i = 0; i < 32; i++) {
            buf[i] = loop * 32U + i;
        }
        /* by-VA family */
        ArchDCacheFlushByVa((UINTPTR)buf, sizeof(buf));
        ArchDCacheCleanByVa((UINTPTR)buf, sizeof(buf));
        ArchDCacheInvByVa((UINTPTR)buf, sizeof(buf));
        ArchICacheFlushByVa((UINTPTR)buf, sizeof(buf));
        /* by-addr family */
        ArchICacheFlushByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
        ArchDCacheFlushByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
        ArchDCacheCleanByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
        ArchDCacheInvByAddr((UINTPTR)buf, (UINTPTR)buf + sizeof(buf));
        /* whole-cache family */
        ArchICacheFlush();
        ArchDCacheFlush();
        ArchDCacheInvalidate();
        /* data integrity after the full sequence */
        for (i = 0; i < 32; i++) {
            ICUNIT_ASSERT_EQUAL(buf[i], loop * 32U + i, buf[i]);
        }
    }
    return LOS_OK;
}

/**
 * @ingroup TEST_CACHE
 * @par TestCase_Number
 * ItLosCache017
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interrupt-safety regression of the source cache impl
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: loop CACHE_INTSAFE_LOOPS times.
 * step2: each iteration writes a buffer, then exercises every by-VA, by-addr
 *        and whole-cache maintenance entry, and verifies the buffer.
 * @par TestCase_Expected_Result
 * 1.No hang/fault across all iterations; data integrity preserved.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */

VOID ItLosCache017(VOID)
{
    TEST_ADD_CASE("ItLosCache017", TestCase, TEST_LOS, TEST_CACHE, TEST_LEVEL0, TEST_FUNCTION);
}

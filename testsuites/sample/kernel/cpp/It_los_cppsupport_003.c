/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: cppsupport test case 003
 */

/* 003: BEFORE/AFTER_SCATTER stage filtering */

#include "osTest.h"
#include "It_los_cppsupport.h"

/* ItLosCppsupport003: BEFORE_SCATTER / AFTER_SCATTER stage filtering — the
 * two-arg API this module replaced could not do this.
 *
 * LOS_CppSystemInit classifies each entry by the CONSTRUCTOR'S CODE ADDRESS
 * versus __fast_end (board.ld: end of the ITCM fast-resident region):
 *   - BEFORE_SCATTER runs only entries at/below __fast_end (fast region);
 *   - AFTER_SCATTER  runs only entries above it;
 *   - NO_SCATTER     runs everything.
 *
 * One fixture function is placed in ITCM via .nor.itcm.text (the SFC driver's
 * placement idiom, board.ld copies it with .itcm_resident) and one stays in
 * flash XIP, so both filter branches are exercised deterministically. */

/* 003: BEFORE/AFTER_SCATTER stage filtering */

/* board.ld absolute symbol: end of the ITCM fast region */
extern CHAR __fast_end;

#define CPP_TEST_ITCM_SECT __attribute__((section(".nor.itcm.text"), noinline))

static volatile UINT32 g_cppFastRan;
static volatile UINT32 g_cppSlowRan;

static CPP_TEST_ITCM_SECT VOID CppCtorFast(VOID)
{
    g_cppFastRan++;
}

static VOID CppCtorSlow(VOID)
{
    g_cppSlowRan++;
}

static UINT32 TestCase(VOID)
{
    UINTPTR array[2];
    INT32 ret;

    ICUNIT_ASSERT_EQUAL(((UINTPTR)CppCtorFast <= (UINTPTR)&__fast_end), 1, (UINTPTR)CppCtorFast);
    ICUNIT_ASSERT_EQUAL(((UINTPTR)CppCtorSlow > (UINTPTR)&__fast_end), 1, (UINTPTR)CppCtorSlow);

    array[0] = (UINTPTR)CppCtorFast;
    array[1] = (UINTPTR)CppCtorSlow;

    /* BEFORE_SCATTER: only fast entry runs */
    g_cppFastRan = 0;
    g_cppSlowRan = 0;
    ret = LOS_CppSystemInit((UINTPTR)&array[0], (UINTPTR)&array[2], BEFORE_SCATTER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(g_cppFastRan, 1, g_cppFastRan);
    ICUNIT_ASSERT_EQUAL(g_cppSlowRan, 0, g_cppSlowRan);

    /* AFTER_SCATTER: only slow entry runs */
    g_cppFastRan = 0;
    g_cppSlowRan = 0;
    ret = LOS_CppSystemInit((UINTPTR)&array[0], (UINTPTR)&array[2], AFTER_SCATTER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(g_cppFastRan, 0, g_cppFastRan);
    ICUNIT_ASSERT_EQUAL(g_cppSlowRan, 1, g_cppSlowRan);

    /* NO_SCATTER */
    g_cppFastRan = 0;
    g_cppSlowRan = 0;
    ret = LOS_CppSystemInit((UINTPTR)&array[0], (UINTPTR)&array[2], NO_SCATTER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(g_cppFastRan, 1, g_cppFastRan);
    ICUNIT_ASSERT_EQUAL(g_cppSlowRan, 1, g_cppSlowRan);

    return LOS_OK;
}

VOID ItLosCppsupport003(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosCppsupport003", TestCase, TEST_LOS, TEST_FN_KERNELEXT_CPPSUPPORT,
                  TEST_LEVEL0, TEST_FUNCTION);
}

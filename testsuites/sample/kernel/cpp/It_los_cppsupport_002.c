/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: cppsupport test case 002
 */

#include "osTest.h"
#include "It_los_cppsupport.h"

/* ItLosCppsupport002: parameter validation —
 *  - initArrayEnd < initArrayStart: returns LOS_NOK and executes NOTHING;
 *  - empty table (start == end): valid, returns LOS_OK, executes nothing. */

static volatile UINT32 g_cppInvalidRan;

static VOID CppInvalidEntry(VOID)
{
    g_cppInvalidRan++;
}

static UINT32 TestCase(VOID)
{
    UINTPTR array[2];
    INT32 ret;

    g_cppInvalidRan = 0;
    array[0] = (UINTPTR)CppInvalidEntry;
    array[1] = (UINTPTR)CppInvalidEntry;

    /* end < start: rejected, nothing runs */
    ret = LOS_CppSystemInit((UINTPTR)&array[1], (UINTPTR)&array[0], NO_SCATTER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);
    ICUNIT_ASSERT_EQUAL(g_cppInvalidRan, 0, g_cppInvalidRan);

    /* empty table: accepted, nothing runs */
    ret = LOS_CppSystemInit((UINTPTR)&array[0], (UINTPTR)&array[0], NO_SCATTER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(g_cppInvalidRan, 0, g_cppInvalidRan);

    return LOS_OK;
}

VOID ItLosCppsupport002(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosCppsupport002", TestCase, TEST_LOS, TEST_FN_KERNELEXT_CPPSUPPORT,
                  TEST_LEVEL0, TEST_FUNCTION);
}

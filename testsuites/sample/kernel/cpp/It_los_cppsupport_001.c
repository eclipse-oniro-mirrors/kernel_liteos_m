/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: cppsupport test case 001
 */

#include "osTest.h"
#include "It_los_cppsupport.h"

/* ItLosCppsupport001: NO_SCATTER executes every .init_array entry once, in
 * table order, and returns LOS_OK. Uses a synthetic array — LOS_CppSystemInit
 * takes caller-supplied bounds, so the walk is unit-testable without touching
 * the real constructor table. */

static volatile UINT32 g_cppOrder;
static volatile UINT32 g_cppStep1;
static volatile UINT32 g_cppStep2;
static volatile UINT32 g_cppStep3;

static VOID CppStep1(VOID)
{
    g_cppStep1 = ++g_cppOrder;
}

static VOID CppStep2(VOID)
{
    g_cppStep2 = ++g_cppOrder;
}

static VOID CppStep3(VOID)
{
    g_cppStep3 = ++g_cppOrder;
}

static UINT32 TestCase(VOID)
{
    UINTPTR array[3];
    INT32 ret;

    g_cppOrder = 0;
    g_cppStep1 = 0;
    g_cppStep2 = 0;
    g_cppStep3 = 0;

    array[0] = (UINTPTR)CppStep1;
    array[1] = (UINTPTR)CppStep2;
    array[2] = (UINTPTR)CppStep3;

    ret = LOS_CppSystemInit((UINTPTR)&array[0], (UINTPTR)&array[3], NO_SCATTER);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* all entries ran, exactly once each, in table order */
    ICUNIT_ASSERT_EQUAL(g_cppStep1, 1, g_cppStep1);
    ICUNIT_ASSERT_EQUAL(g_cppStep2, 2, g_cppStep2);
    ICUNIT_ASSERT_EQUAL(g_cppStep3, 3, g_cppStep3);
    ICUNIT_ASSERT_EQUAL(g_cppOrder, 3, g_cppOrder);

    return LOS_OK;
}

VOID ItLosCppsupport001(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosCppsupport001", TestCase, TEST_LOS, TEST_FN_KERNELEXT_CPPSUPPORT,
                  TEST_LEVEL0, TEST_FUNCTION);
}

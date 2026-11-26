/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: cppsupport test case 004
 */

#include "osTest.h"
#include "It_los_cppsupport.h"

/* ItLosCppsupport004: C++ global-object lifecycle on this firmware.
 *
 * The fixture object in cppsupport_cpp_obj.cpp has BOTH a constructor and a
 * destructor:
 *  - its ctor ran from the .init_array walk in board main()
 *    (LOS_CppSystemInit, NO_SCATTER) exactly once, BEFORE the scheduler
 *    started — asserted via the ctor counter and the magic it stores;
 *  - its dtor registration resolved through the cppsupport
 *    __cxa_atexit/__dso_handle stubs (without them the link would fail with
 *    undefined symbols) — asserted by calling the stub the same way the
 *    compiler-emitted ctor code does;
 *  - the destructor itself NEVER runs (no exit()/fini_array walk) — nothing
 *    to assert positively; the dtor counter staying at 0 over the whole
 *    suite run is the ongoing evidence. */

static UINT32 TestCase(VOID)
{
    /* Constructor ran exactly once, before the scheduler started. */
    ICUNIT_ASSERT_EQUAL(CppTestCtorCount(), 1, CppTestCtorCount());
    /* Object state written by the constructor is intact. */
    ICUNIT_ASSERT_EQUAL(CppTestMagic(), 0xC0DEC0DE, CppTestMagic());
    /* __cxa_atexit/__dso_handle stub callable exactly like the compiler
     * emits it from a destructor-bearing ctor. */
    ICUNIT_ASSERT_EQUAL(CppTestCxaAtexitCall(), 0, CppTestCxaAtexitCall());

    return LOS_OK;
}

VOID ItLosCppsupport004(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosCppsupport004", TestCase, TEST_LOS, TEST_FN_KERNELEXT_CPPSUPPORT,
                  TEST_LEVEL0, TEST_FUNCTION);
}

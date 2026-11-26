/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: cppsupport test case 005
 */

/* 005: global destructor semantics (from language-suite 042) */

#include "It_los_cppsupport.h"

extern "C" {
extern char __fini_array_start;
extern char __fini_array_end;
}

unsigned int g_cppApiDtorCount = 0;
unsigned int g_cppApiDtorFiniFlag = 0;

class TestDtorGlobal {
public:
    TestDtorGlobal() { ++g_cppApiDtorCount; }
    ~TestDtorGlobal() {} /* registered via __cxa_atexit, never auto-runs */
};

TestDtorGlobal g_cppApiDtorObj; /* the global under test */

static void CppApiDtorFiniEntry(void)
{
    g_cppApiDtorFiniFlag = 0x42;
}

__attribute__((section(".fini_array"), used)) static void (*g_cppApiDtorFiniEntry)(void) = CppApiDtorFiniEntry;

static UINT32 TestCase(VOID)
{
    /* ctor ran once via the walk */
    ICUNIT_ASSERT_EQUAL(g_cppApiDtorCount, 1, g_cppApiDtorCount);

    /* dtor not auto-invoked */
    ICUNIT_ASSERT_EQUAL(g_cppApiDtorFiniFlag, 0, g_cppApiDtorFiniFlag);

    /* manual trigger: reverse walk */
    typedef void (*FiniFn)(void);
    FiniFn *begin = reinterpret_cast<FiniFn *>(&__fini_array_start);
    FiniFn *end = reinterpret_cast<FiniFn *>(&__fini_array_end);
    FiniFn *it;
    for (it = end; it > begin;) {
        --it;
        (*it)();
    }
    ICUNIT_ASSERT_EQUAL(g_cppApiDtorFiniFlag, 0x42, g_cppApiDtorFiniFlag);
    return LOS_OK;
}

VOID ItLosCppsupport005(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE((iCHAR *)"ItLosCppsupport005", TestCase, TEST_LOS, TEST_FN_KERNELEXT_CPPSUPPORT,
                  TEST_LEVEL0, TEST_FUNCTION);
}

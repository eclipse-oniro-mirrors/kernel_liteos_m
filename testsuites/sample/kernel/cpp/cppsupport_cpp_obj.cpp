/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: cppsupport C++ test fixture
 */

extern "C" {
typedef void (*CppTestExitFunc)(void *);
void __cxa_atexit(CppTestExitFunc func, void *arg1, void *arg2);
extern void *__dso_handle;

unsigned int CppTestCtorCount(void);
unsigned int CppTestMagic(void);
unsigned int CppTestCxaAtexitCall(void);
}

namespace {
const unsigned int CPP_TEST_MAGIC = 0xC0DEC0DEU;
unsigned int g_cppTestCtorCount = 0;
unsigned int g_cppTestMagic = 0;

void CppTestNeverRuns(void *)
{
    /* Destructor body — registered via __cxa_atexit, never executed. */
    g_cppTestMagic = 0xDEADBEEFU;
}
} // namespace

class CppTestGlobalObject {
public:
    CppTestGlobalObject() : guard_(CPP_TEST_MAGIC)
    {
        ++g_cppTestCtorCount;
        g_cppTestMagic = CPP_TEST_MAGIC;
    }

    ~CppTestGlobalObject()
    {
        /* Intentionally empty: it must never run. CppTestNeverRuns above is
         * the observable stand-in for "would the dtor machinery fire". */
    }

private:
    unsigned int guard_;
};

static CppTestGlobalObject g_cppTestObj;

unsigned int CppTestCtorCount(void)
{
    return g_cppTestCtorCount;
}

unsigned int CppTestMagic(void)
{
    return g_cppTestMagic;
}

unsigned int CppTestCxaAtexitCall(void)
{
    /* Same call shape the compiler emits from a destructor-bearing ctor. */
    __cxa_atexit(CppTestNeverRuns, 0, &__dso_handle);
    return 0;
}

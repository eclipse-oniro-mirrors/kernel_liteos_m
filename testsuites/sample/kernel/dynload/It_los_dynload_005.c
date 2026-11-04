/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "los_ld_elflib.h"
#include "It_los_dynload.h"

/* Test the dep_a.so -> dep_b.so DT_NEEDED chain (implicit load, recursive unload). */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    INT32 (*fnA)(INT32) = NULL;
    VOID *symAddr = NULL;
    INT32 ret;

    handle = LOS_SoLoad(DSO_FULL_PATH("dep_a.so"));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    fnA = (INT32 (*)(INT32))LOS_FindSymByName(handle, "dep_a_func");
    ICUNIT_GOTO_NOT_EQUAL(fnA, NULL, fnA, EXIT);
    /* dep_b_func(1) == 1001, dep_a_func adds 2000 */
    ret = fnA(1);
    ICUNIT_GOTO_EQUAL(ret, 3001, ret, EXIT);

    /* dep_b.so is loaded implicitly: its symbols are visible globally */
    symAddr = LOS_FindSymByName(NULL, "dep_b_func");
    ICUNIT_GOTO_NOT_EQUAL(symAddr, NULL, symAddr, EXIT);

    /* unload dep_a.so: recursive unload must release dep_b.so too */
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    symAddr = LOS_FindSymByName(NULL, "dep_b_func");
    ICUNIT_ASSERT_EQUAL(symAddr, NULL, symAddr);

    symAddr = LOS_FindSymByName(NULL, "dep_a_func");
    ICUNIT_ASSERT_EQUAL(symAddr, NULL, symAddr);

    /* dep_b.so can be loaded standalone afterwards */
    handle = LOS_SoLoad(DSO_FULL_PATH("dep_b.so"));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    fnA = (INT32 (*)(INT32))LOS_FindSymByName(handle, "dep_b_func");
    ICUNIT_GOTO_NOT_EQUAL(fnA, NULL, fnA, EXIT2);
    ret = fnA(1);
    ICUNIT_GOTO_EQUAL(ret, 1001, ret, EXIT2);

EXIT2:
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;

EXIT:
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosDynload005(VOID)
{
    TEST_ADD_CASE("ItLosDynload005", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

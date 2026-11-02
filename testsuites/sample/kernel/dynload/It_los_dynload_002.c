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

/* Test loading a shared object from the filesystem, calling it and unloading */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    INT32 (*fnAdd)(INT32, INT32) = NULL;
    INT32 (*fnGet)(VOID) = NULL;
    INT32 ret;

    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    fnGet = (INT32 (*)(VOID))LOS_FindSymByName(handle, "get_basic_value");
    ICUNIT_GOTO_NOT_EQUAL(fnGet, NULL, fnGet, EXIT);
    ret = fnGet();
    ICUNIT_GOTO_EQUAL(ret, 100, ret, EXIT);

    fnAdd = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "add_basic");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd, NULL, fnAdd, EXIT);
    ret = fnAdd(2, 3);
    ICUNIT_GOTO_EQUAL(ret, 105, ret, EXIT);

    /* unknown symbol in this module */
    fnAdd = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "no_such_sym");
    ICUNIT_GOTO_EQUAL(fnAdd, NULL, fnAdd, EXIT);

EXIT:
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosDynload002(VOID)
{
    TEST_ADD_CASE("ItLosDynload002", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

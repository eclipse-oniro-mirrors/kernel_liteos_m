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

/* Test the OS symbol table through g_dynload_os_export (see It_los_dynload_dso.c). */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    INT32 (*fnGet)(VOID) = NULL;
    INT32 ret;

    handle = LOS_SoLoad(DSO_FULL_PATH("ossym.so"));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    fnGet = (INT32 (*)(VOID))LOS_FindSymByName(handle, "get_os_export");
    ICUNIT_GOTO_NOT_EQUAL(fnGet, NULL, fnGet, EXIT);
    ret = fnGet();
    ICUNIT_GOTO_EQUAL(ret, 4242, ret, EXIT);

    /* global lookup (handle == NULL) also sees the module's symbols */
    fnGet = (INT32 (*)(VOID))LOS_FindSymByName(NULL, "get_os_export");
    ICUNIT_GOTO_NOT_EQUAL(fnGet, NULL, fnGet, EXIT);

    /* and the kernel-side symbol is not visible as a module symbol */
    fnGet = (INT32 (*)(VOID))LOS_FindSymByName(handle, "g_dynload_os_export");
    ICUNIT_GOTO_EQUAL(fnGet, NULL, fnGet, EXIT);

EXIT:
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosDynload004(VOID)
{
    TEST_ADD_CASE("ItLosDynload004", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

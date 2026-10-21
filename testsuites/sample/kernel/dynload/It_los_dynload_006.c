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

/* Test reference counting: repeated load returns the same handle */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle1 = NULL;
    VOID *handle2 = NULL;
    INT32 (*fnAdd)(INT32, INT32) = NULL;
    INT32 ret;

    handle1 = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_ASSERT_NOT_EQUAL(handle1, NULL, handle1);

    handle2 = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_GOTO_NOT_EQUAL(handle2, NULL, handle2, EXIT1);
    ICUNIT_GOTO_EQUAL(handle1, handle2, handle2, EXIT1);

    /* one unload keeps the module alive (refcount drops to 1) */
    ret = LOS_ModuleUnload(handle1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT1);
    handle1 = NULL; /* first reference consumed; avoid double unload in EXIT1 */

    /* both loads above and this unload left the pool R+W; arm W^X before the call */
    IT_DYNLOAD_WX_ON();

    fnAdd = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle2, "add_basic");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd, NULL, fnAdd, EXIT1);
    ret = fnAdd(1, 1);
    ICUNIT_GOTO_EQUAL(ret, 102, ret, EXIT1);

    /* second unload releases the module */
    ret = LOS_ModuleUnload(handle2);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    fnAdd = (INT32 (*)(INT32, INT32))LOS_FindSymByName(NULL, "add_basic");
    ICUNIT_ASSERT_EQUAL(fnAdd, NULL, fnAdd);

    /* invalid handle is rejected */
    ret = LOS_ModuleUnload(handle2);
    ICUNIT_ASSERT_NOT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;

EXIT1:
    if (handle2 != NULL) {
        ret = LOS_ModuleUnload(handle2);
        ICUNIT_TRACK_EQUAL(ret, LOS_OK, ret);
    }
    if (handle1 != NULL) {
        ret = LOS_ModuleUnload(handle1);
        ICUNIT_TRACK_EQUAL(ret, LOS_OK, ret);
    }
    return LOS_OK;
}

VOID ItLosDynload006(VOID)
{
    TEST_ADD_CASE("ItLosDynload006", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

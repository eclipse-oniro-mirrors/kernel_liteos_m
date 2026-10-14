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
 * WHETHER IN CONTRACT, STRICT LIABILITY, TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include "los_ld_elflib.h"
#include "It_los_dynload.h"

/* Image boundaries provided by the objcopy objects (same file as written to the FS). */
extern const UINT8 _binary_objbasic_o_start[];
extern const UINT8 _binary_objbasic_o_end[];
extern const UINT8 _binary_objundef_o_start[];
extern const UINT8 _binary_objundef_o_end[];

/* Test relocatable objects from memory, OS symbol resolution and undefined-symbol rejection. */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    VOID *badHandle = NULL;
    INT32 (*fn)(INT32, INT32) = NULL;
    INT32 ret;

    /* memory source: ET_OBJ buffer, sections copied out of the buffer */
    handle = LOS_MemLoad("objbasic_mem", strlen("objbasic_mem"), (const CHAR *)_binary_objbasic_o_start,
                         (UINT32)(_binary_objbasic_o_end - _binary_objbasic_o_start));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "obj_add");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT);
    ret = fn(1, 2); /* obj_helper(1, 2) * 2 = (1+2+3)*2 */
    ICUNIT_GOTO_EQUAL(ret, 12, ret, EXIT);

    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* .o resolving a symbol through the OS symbol table */
    handle = LOS_ObjLoad(DSO_FULL_PATH("objossym.o"));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "obj_get_os_export");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT);
    ret = fn(0, 0); /* g_dynload_os_export registered as 4242 */
    ICUNIT_GOTO_EQUAL(ret, 4242, ret, EXIT);

    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "obj_add_os_export");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT);
    ret = fn(58, 0); /* 58 + 4242 */
    ICUNIT_GOTO_EQUAL(ret, 4300, ret, EXIT);

    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* unresolved symbol: both load paths must reject the object */
    badHandle = LOS_ObjLoad(DSO_FULL_PATH("objundef.o"));
    ICUNIT_ASSERT_EQUAL(badHandle, NULL, badHandle);

    badHandle = LOS_MemLoad("objundef_mem", strlen("objundef_mem"), (const CHAR *)_binary_objundef_o_start,
                            (UINT32)(_binary_objundef_o_end - _binary_objundef_o_start));
    ICUNIT_ASSERT_EQUAL(badHandle, NULL, badHandle);

    return LOS_OK;

EXIT:
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosDynload008(VOID)
{
    TEST_ADD_CASE("ItLosDynload008", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

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

/* Test LOS_ObjLoad on objbasic.o (THM_CALL/JUMP24/ABS32) and objmovwt.o (MOVW/MOVT). */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    INT32 (*fn)(INT32, INT32) = NULL;
    INT32 ret;

    handle = LOS_ObjLoad(DSO_FULL_PATH("objbasic.o"));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    /* plain BL: R_ARM_THM_CALL, obj_helper(20, 22) * 2 = (20+22+3)*2 */
    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "obj_add");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT);
    ret = fn(20, 22);
    ICUNIT_GOTO_EQUAL(ret, 90, ret, EXIT);

    /* tail call: R_ARM_THM_JUMP24, obj_helper(5, 6) = 5+6+3 */
    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "obj_tail_call");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT);
    ret = fn(5, 6);
    ICUNIT_GOTO_EQUAL(ret, 14, ret, EXIT);

    /* text -> rodata literal pool: R_ARM_ABS32 */
    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "obj_get_ro");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT);
    ret = fn(0, 0);
    ICUNIT_GOTO_EQUAL(ret, 40, ret, EXIT);

    /* ABS32-relocated code pointers in .rodata, called at runtime */
    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "obj_call_by_table");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT);
    ret = fn(2, 3);
    ICUNIT_GOTO_EQUAL(ret, 208, ret, EXIT);
    ret = fn(3, 4);
    ICUNIT_GOTO_EQUAL(ret, 220, ret, EXIT);

    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* MOVW/MOVT materialization: data, array and code addresses */
    handle = LOS_ObjLoad(DSO_FULL_PATH("objmovwt.o"));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "movwt_read_data");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT1);
    ret = fn(0, 0);
    ICUNIT_GOTO_EQUAL(ret, 500, ret, EXIT1);

    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "movwt_read_array");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT1);
    ret = fn(2, 0);
    ICUNIT_GOTO_EQUAL(ret, 15, ret, EXIT1);

    fn = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "movwt_indirect");
    ICUNIT_GOTO_NOT_EQUAL(fn, NULL, fn, EXIT1);
    ret = fn(7, 3); /* odd: MOVW/MOVT address of movwt_add, 7+3 */
    ICUNIT_GOTO_EQUAL(ret, 10, ret, EXIT1);
    ret = fn(8, 3); /* even: movwt_sub, 8-3 */
    ICUNIT_GOTO_EQUAL(ret, 5, ret, EXIT1);

    /* a data symbol of the module resolves too */
    {
        INT32 *val = (INT32 *)LOS_FindSymByName(handle, "g_movwt_val");
        ICUNIT_GOTO_NOT_EQUAL(val, NULL, val, EXIT1);
        ICUNIT_GOTO_EQUAL(*val, 500, *val, EXIT1);
    }

EXIT1:
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;

EXIT:
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosDynload007(VOID)
{
    TEST_ADD_CASE("ItLosDynload007", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

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

/* Image boundaries provided by the objcopy objects (same file as written to the FS). */
extern const UINT8 _binary_basic_so_start[];
extern const UINT8 _binary_basic_so_end[];

/* Test loading a shared object from a memory buffer */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    VOID *badHandle = NULL;
    INT32 (*fnAdd)(INT32, INT32) = NULL;
    INT32 ret;

    handle = LOS_MemLoad("basic_mem", strlen("basic_mem"), (const CHAR *)_binary_basic_so_start,
                         (UINT32)(_binary_basic_so_end - _binary_basic_so_start));
    ICUNIT_ASSERT_NOT_EQUAL(handle, NULL, handle);

    IT_DYNLOAD_WX_ON();

    fnAdd = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "add_basic");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd, NULL, fnAdd, EXIT);
    ret = fnAdd(20, 22);
    ICUNIT_GOTO_EQUAL(ret, 142, ret, EXIT);

    /* invalid buffers */
    badHandle = LOS_MemLoad("bad1", strlen("bad1"), NULL, 16);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    badHandle = LOS_MemLoad("bad2", strlen("bad2"), (const CHAR *)_binary_basic_so_start, 4);
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

EXIT:
    ret = LOS_ModuleUnload(handle);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

VOID ItLosDynload003(VOID)
{
    TEST_ADD_CASE("ItLosDynload003", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
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
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF ANY SUCH DAMAGE.
 */
#include "los_ld_elflib.h"
#include "It_los_dynload.h"

extern const UINT8 _binary_basic_so_start[];
extern const UINT8 _binary_basic_so_end[];

/* 用例简要描述: LdDestroy 销毁加载器与懒重建 */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    VOID *symAddr = NULL;
    INT32 (*fnAdd)(INT32, INT32) = NULL;
    INT32 ret;

    /* arrange：加载依赖链（dep_a 隐式拉起非 SOHEAD 的 dep_b）与可重定位对象 */
    handle = LOS_SoLoad(DSO_FULL_PATH("dep_a.so"));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    handle = LOS_ObjLoad(DSO_FULL_PATH("objbasic.o"));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    symAddr = LOS_FindSymByName(handle, "obj_add");
    ICUNIT_GOTO_NOT_EQUAL(symAddr, NULL, symAddr, EXIT);

    /* 销毁加载器：强制卸载 ET_OBJ/依赖链模块并复位内部状态 */
    LOS_LdDestroy();

    /* 销毁后模块符号经全局查找不再可见 */
    symAddr = LOS_FindSymByName(NULL, (CHAR *)"obj_add");
    ICUNIT_GOTO_EQUAL(symAddr, NULL, symAddr, EXIT);

    /* 加载器未初始化时再次销毁安全（幂等） */
    LOS_LdDestroy();
    IT_DYNLOAD_EXEC_POOL_SET(); /* W^X 板：重注册专属窗口池，懒重建的模块才可执行 */

    /* 销毁后经内存缓冲懒重建（加载器未初始化时的 MemLoad 路径） */
    handle = LOS_MemLoad("rebuild", strlen("rebuild"), (const CHAR *)_binary_basic_so_start,
                         (UINT32)(_binary_basic_so_end - _binary_basic_so_start));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    IT_DYNLOAD_WX_ON();
    fnAdd = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "add_basic");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd, NULL, fnAdd, EXIT);
    ret = fnAdd(20, 22);
    ICUNIT_GOTO_EQUAL(ret, 142, ret, EXIT);
    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    /* 销毁清空路径表后裸名解析失败（绝对路径仍可用） */
    handle = LOS_SoLoad("dep_a.so");
    ICUNIT_GOTO_EQUAL(handle, NULL, handle, EXIT);

    /* 恢复：销毁已清空路径表，重注册套件搜索路径 */
    (VOID)LOS_PathAdd((CHAR *)DSO_PATH);
    return LOS_OK;

EXIT:
    if (handle != NULL) {
        (VOID)LOS_ModuleUnload(handle);
    }
    (VOID)LOS_PathAdd((CHAR *)DSO_PATH);
    return LOS_OK;
}

VOID ItLosDynload012(VOID)
{
    TEST_ADD_CASE("ItLosDynload012", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

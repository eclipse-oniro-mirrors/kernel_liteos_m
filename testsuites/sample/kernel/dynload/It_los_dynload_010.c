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

/* 用例简要描述: PathAdd 路径注册校验与裸名解析生效 */
STATIC UINT32 TestCase(VOID)
{
    CHAR longPath[PATH_MAX + 4];
    VOID *handle = NULL;
    INT32 ret;
    UINT32 idx;

    /* NULL 与空串被拒绝 */
    ret = LOS_PathAdd(NULL);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);
    ret = LOS_PathAdd((CHAR *)"");
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* 重复路径被拒绝（套件已注册 DSO_PATH，尾部斜杠归一化后仍视为重复） */
    ret = LOS_PathAdd((CHAR *)DSO_PATH);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);
    ret = LOS_PathAdd((CHAR *)DSO_PATH "///");
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* 超长路径（长度不小于 PATH_MAX）被拒绝 */
    for (idx = 0; idx < sizeof(longPath) - 1; idx++) {
        longPath[idx] = 'a';
    }
    longPath[sizeof(longPath) - 1] = '\0';
    ret = LOS_PathAdd(longPath);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* 新路径注册成功 */
    ret = LOS_PathAdd((CHAR *)"/littlefs");
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* 裸名经已注册搜索路径解析并加载成功 */
    handle = LOS_SoLoad("basic.so");
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    /* 恢复套件状态：销毁清空路径表（含本用例注册项）→ 重注册搜索路径 → 重建加载器（ResLeak 中性） */
    LOS_LdDestroy();
    (VOID)LOS_PathAdd((CHAR *)DSO_PATH);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    (VOID)LOS_ModuleUnload(handle);
    return LOS_OK;

EXIT:
    if (handle != NULL) {
        (VOID)LOS_ModuleUnload(handle);
    }
    LOS_LdDestroy();
    (VOID)LOS_PathAdd((CHAR *)DSO_PATH);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    (VOID)LOS_ModuleUnload(handle);
    return LOS_OK;
}

VOID ItLosDynload010(VOID)
{
    TEST_ADD_CASE("ItLosDynload010", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

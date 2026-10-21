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

/* 用例简要描述: DynParamReg 参数校验与 ZIP/NOZIP 策略注册生效 */
STATIC UINT32 TestCase(VOID)
{
    DYNLOAD_PARAM_S param;
    VOID *handle = NULL;
    INT32 (*fnAdd)(INT32, INT32) = NULL;
    INT32 ret;

    /* NULL 参数被拒绝，策略保持默认，加载仍成功 */
    LOS_DynParamReg(NULL);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    /* 非法策略值被拒绝，默认策略不受影响 */
    param.enLoadStrategy = (enum LOAD_STRATEGY)0x7F;
    LOS_DynParamReg(&param);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    /* ZIP 策略注册生效：加载与符号调用走 ZIP 内存加载路径 */
    param.enLoadStrategy = ZIP;
    LOS_DynParamReg(&param);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    IT_DYNLOAD_WX_ON();
    fnAdd = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "add_basic");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd, NULL, fnAdd, EXIT);
    ret = fnAdd(20, 22);
    ICUNIT_GOTO_EQUAL(ret, 142, ret, EXIT);
    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    /* ZIP 路径下 ELF 头校验失败（ET_EXEC）被拒绝 */
    handle = LOS_SoLoad(DSO_FULL_PATH("exec.so"));
    ICUNIT_GOTO_EQUAL(handle, NULL, handle, EXIT);

    /* 恢复默认 NOZIP 策略，不向后续用例泄漏全局状态 */
    param.enLoadStrategy = NOZIP;
    LOS_DynParamReg(&param);

    return LOS_OK;

EXIT:
    if (handle != NULL) {
        (VOID)LOS_ModuleUnload(handle);
    }
    param.enLoadStrategy = NOZIP;
    LOS_DynParamReg(&param);
    return LOS_OK;
}

VOID ItLosDynload009(VOID)
{
    TEST_ADD_CASE("ItLosDynload009", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

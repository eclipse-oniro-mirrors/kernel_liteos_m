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
#include "los_memory.h"
#include "It_los_dynload.h"

#define DYNLOAD_TEST_POOL_SIZE 0x1800

/* 用例简要描述: DynMemPoolSet 内存池设置与池上加载 */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    VOID *poolMem = NULL;
    VOID *pool = NULL;
    INT32 (*fnAdd)(INT32, INT32) = NULL;
    INT32 ret;

    /* arrange：经一次加载触发懒初始化，确保加载器处于已初始化状态 */
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    /* NULL 池被拒绝 */
    ret = LOS_DynMemPoolSet(NULL);
    ICUNIT_GOTO_EQUAL(ret, FALSE, ret, EXIT);

    /* 加载器已初始化时设置内存池被拒绝（临时池尚未分配，任意合法指针等价） */
    ret = LOS_DynMemPoolSet((VOID *)&ret);
    ICUNIT_GOTO_EQUAL(ret, FALSE, ret, EXIT);

    /* 销毁加载器后设置外部内存池成功。外部池从系统堆临时分配（8 字节对齐
     * 后 MemInit），用例结束真实释放：静态数组会压缩全周程可用堆，把
     * Shell/FS 域的堆敏感用例压垮（hi3322 系统堆仅 ~190KB） */
    LOS_LdDestroy();
    poolMem = LOS_MemAlloc((VOID *)m_aucSysMem0, DYNLOAD_TEST_POOL_SIZE + 8);
    ICUNIT_GOTO_NOT_EQUAL(poolMem, NULL, poolMem, EXIT);
    pool = (VOID *)(((UINTPTR)poolMem + 7) & ~(UINTPTR)7);
    ret = LOS_MemInit(pool, DYNLOAD_TEST_POOL_SIZE);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_DynMemPoolSet(pool);
    ICUNIT_GOTO_EQUAL(ret, TRUE, ret, EXIT);

    /* DSO 从外部内存池加载：模块符号必须落在外部池区间内 */
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);
    fnAdd = (INT32 (*)(INT32, INT32))LOS_FindSymByName(handle, "add_basic");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd, NULL, fnAdd, EXIT);
    ICUNIT_GOTO_EQUAL((((UINTPTR)fnAdd >= (UINTPTR)pool) &&
                       ((UINTPTR)fnAdd < ((UINTPTR)pool + DYNLOAD_TEST_POOL_SIZE))), 1,
                      fnAdd, EXIT);
#if !defined(LOSCFG_PLATFORM_HI3322)
    /* 非 W^X 板：外部池内模块可直接执行 */
    IT_DYNLOAD_WX_ON();
    ret = fnAdd(20, 22);
    ICUNIT_GOTO_EQUAL(ret, 142, ret, EXIT);
#endif
    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    /* 加载器已销毁、不再引用外部池：先行释放，后续验证与池无关 */
    LOS_LdDestroy();
    ret = LOS_MemDeInit(pool);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree((VOID *)m_aucSysMem0, poolMem);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    pool = NULL;
    poolMem = NULL;

#if defined(LOSCFG_PLATFORM_HI3322)
    /* W^X 板：外部普通内存不可执行，"池上加载+执行"经板级专属窗口验证
     * （与板级 main.c 启动注册同一池，销毁后重注册路径） */
    ret = LOS_DynMemPoolSet((VOID *)IT_DYNLOAD_WX_POOL_BASE);
    ICUNIT_GOTO_EQUAL(ret, TRUE, ret, EXIT);
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
#endif

    /* 恢复：销毁复位内存池引用 → 重注册搜索路径 → 重建加载器回板级默认池（ResLeak 中性） */
    LOS_LdDestroy();
    IT_DYNLOAD_EXEC_POOL_SET();
    (VOID)LOS_PathAdd((CHAR *)DSO_PATH);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    (VOID)LOS_ModuleUnload(handle);
    return LOS_OK;

EXIT:
    if (handle != NULL) {
        (VOID)LOS_ModuleUnload(handle);
    }
    LOS_LdDestroy();
    if (poolMem != NULL) {
        if (pool != NULL) {
            (VOID)LOS_MemDeInit(pool);
        }
        (VOID)LOS_MemFree((VOID *)m_aucSysMem0, poolMem);
    }
    IT_DYNLOAD_EXEC_POOL_SET();
    (VOID)LOS_PathAdd((CHAR *)DSO_PATH);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    (VOID)LOS_ModuleUnload(handle);
    return LOS_OK;
}

VOID ItLosDynload011(VOID)
{
    TEST_ADD_CASE("ItLosDynload011", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

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
#include "securec.h"
#include "unistd.h"
#include "It_los_dynload.h"

#define DYNLOAD_TINY_POOL_SIZE 1024
#define DYNLOAD_HANDLE_NUM 9

extern const UINT8 _binary_basic_so_start[];
extern const UINT8 _binary_basic_so_end[];
extern const UINT8 _binary_ossym_so_start[];
extern const UINT8 _binary_ossym_so_end[];

extern int TestFsVolumeEnsure(void);
extern int TestFsVolumeRelease(void);

/* 用例简要描述: 资源约束下加载失败与句柄池耗尽 */
STATIC UINT32 TestCase(VOID)
{
    VOID *handleDepB = NULL;
    VOID *handleDepA = NULL;
    VOID *handle = NULL;
    VOID *symAddr = NULL;
    VOID *handleOther = NULL;
    VOID *handles[DYNLOAD_HANDLE_NUM] = {NULL};
    VOID *tinyPoolMem = NULL;
    VOID *tinyPool = NULL;
    INT32 (*fnA)(INT32) = NULL;
    CHAR name[16];
    UINT32 imgLen = (UINT32)(_binary_basic_so_end - _binary_basic_so_start);
    INT32 ret;
    INT32 idx;

    /* 依赖已加载：先显式加载 dep_b（其 DT_NEEDED basic.so 随链拉起），
     * 再加载 dep_a 走依赖命中路径（含 dep_b->basic 的递归引用计数） */
    handleDepB = LOS_SoLoad(DSO_FULL_PATH("dep_b.so"));
    ICUNIT_GOTO_NOT_EQUAL(handleDepB, NULL, handleDepB, EXIT);
    handleDepA = LOS_SoLoad(DSO_FULL_PATH("dep_a.so"));
    ICUNIT_GOTO_NOT_EQUAL(handleDepA, NULL, handleDepA, EXIT);
    IT_DYNLOAD_WX_ON();
    fnA = (INT32 (*)(INT32))LOS_FindSymByName(handleDepA, "dep_a_func");
    ICUNIT_GOTO_NOT_EQUAL(fnA, NULL, fnA, EXIT);
    /* dep_b_func(5)==1005, dep_a_func 在其上加 2000（与 ItLosDynload005 的 1001+2000 同源） */
    ret = fnA(5);
    ICUNIT_GOTO_EQUAL(ret, 3005, ret, EXIT);
    ret = LOS_ModuleUnload(handleDepA);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handleDepA = NULL;
    ret = LOS_ModuleUnload(handleDepB);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handleDepB = NULL;

    /* 依赖文件缺失：删除 dep_b.so 后加载 dep_a 被拒绝（DT_NEEDED 解析失败） */
    (VOID)unlink(DSO_FULL_PATH("dep_b.so"));
    handleDepA = LOS_SoLoad(DSO_FULL_PATH("dep_a.so"));
    ICUNIT_GOTO_EQUAL(handleDepA, NULL, handleDepA, EXIT);
    handleDepA = NULL;
    ItLosDynloadPrepareDsos(); /* 恢复被删除的 DSO 文件 */

    /* 极小内存池上分配失败：加载被拒绝（池从系统堆临时分配，用例内释放，
     * 避免 .bss 常驻压缩全周程堆预算） */
    LOS_LdDestroy();
    tinyPoolMem = LOS_MemAlloc((VOID *)m_aucSysMem0, DYNLOAD_TINY_POOL_SIZE + 8);
    ICUNIT_GOTO_NOT_EQUAL(tinyPoolMem, NULL, tinyPoolMem, EXIT);
    tinyPool = (VOID *)(((UINTPTR)tinyPoolMem + 7) & ~(UINTPTR)7);
    ret = LOS_MemInit(tinyPool, DYNLOAD_TINY_POOL_SIZE);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_DynMemPoolSet(tinyPool);
    ICUNIT_GOTO_EQUAL(ret, TRUE, ret, EXIT);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_GOTO_EQUAL(handle, NULL, handle, EXIT);
    LOS_LdDestroy();
    ret = LOS_MemDeInit(tinyPool);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree((VOID *)m_aucSysMem0, tinyPoolMem);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    tinyPool = NULL;
    tinyPoolMem = NULL;
    IT_DYNLOAD_EXEC_POOL_SET(); /* W^X 板：重注册窗口池，句柄池耗尽段的模块统一从窗口加载 */

    /* 句柄池耗尽：释放 FS 卷腾出堆后，加载不含 add_basic 的 ossym 与 9 个 basic 副本
     * （symtbl+ossym+9 = N_MAX_MODULES=11 槽占满），下一个被拒绝 */
    (VOID)TestFsVolumeRelease();
    handleOther = LOS_MemLoad("ossym_mem", strlen("ossym_mem"), (const CHAR *)_binary_ossym_so_start,
                             (UINT32)(_binary_ossym_so_end - _binary_ossym_so_start));
    ICUNIT_GOTO_NOT_EQUAL(handleOther, NULL, handleOther, EXIT);
    for (idx = 0; idx < DYNLOAD_HANDLE_NUM; idx++) {
        (VOID)sprintf_s(name, sizeof(name), "exhaust%d", idx);
        handles[idx] = LOS_MemLoad(name, strlen(name), (const CHAR *)_binary_basic_so_start, imgLen);
        ICUNIT_GOTO_NOT_EQUAL(handles[idx], NULL, handles[idx], EXIT);
    }
    handle = LOS_MemLoad("exhaust9", strlen("exhaust9"), (const CHAR *)_binary_basic_so_start, imgLen);
    ICUNIT_GOTO_EQUAL(handle, NULL, handle, EXIT);

    /* 同名符号跨模块重定义：句柄所属模块无该符号时的全局兜底查找被拒绝
     * （ossym 无 add_basic，9 个 basic 副本重名 → redefined） */
    symAddr = LOS_FindSymByName(handleOther, (CHAR *)"add_basic");
    ICUNIT_GOTO_EQUAL(symAddr, NULL, symAddr, EXIT);

    ret = LOS_ModuleUnload(handleOther);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handleOther = NULL;

    for (idx = 0; idx < DYNLOAD_HANDLE_NUM; idx++) {
        ret = LOS_ModuleUnload(handles[idx]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        handles[idx] = NULL;
    }

    /* 恢复：重挂 FS 卷并重写 DSO 文件（套件状态不变式） */
    (VOID)TestFsVolumeEnsure();
    ItLosDynloadPrepareDsos();
    (VOID)LOS_PathAdd((CHAR *)DSO_PATH);

    return LOS_OK;

EXIT:
    if (handleDepA != NULL) {
        (VOID)LOS_ModuleUnload(handleDepA);
    }
    if (handleDepB != NULL) {
        (VOID)LOS_ModuleUnload(handleDepB);
    }
    if (handleOther != NULL) {
        (VOID)LOS_ModuleUnload(handleOther);
    }
    for (idx = 0; idx < DYNLOAD_HANDLE_NUM; idx++) {
        if (handles[idx] != NULL) {
            (VOID)LOS_ModuleUnload(handles[idx]);
        }
    }
    LOS_LdDestroy();
    if (tinyPoolMem != NULL) {
        if (tinyPool != NULL) {
            (VOID)LOS_MemDeInit(tinyPool);
        }
        (VOID)LOS_MemFree((VOID *)m_aucSysMem0, tinyPoolMem);
    }
    IT_DYNLOAD_EXEC_POOL_SET();
    (VOID)TestFsVolumeEnsure();
    ItLosDynloadPrepareDsos();
    (VOID)LOS_PathAdd((CHAR *)DSO_PATH);
    handle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    (VOID)LOS_ModuleUnload(handle);
    return LOS_OK;
}

VOID ItLosDynload015(VOID)
{
    TEST_ADD_CASE("ItLosDynload015", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

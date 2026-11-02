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

/*
 * 【现象】ET_REL 对象同名二次加载（LOS_MemLoad/LOS_ObjLoad）返回同一句柄但引用计数
 *        不递增；此时卸载一次即真正释放模块，另一持有者的句柄悬空（It_los_dynload_013
 *        轮 12 实测：二次加载后 unload 一次，后续 LOS_FindSymByName(handle) 返回 NULL）。
 * 【根因】los_ld_elflib.c LOS_ObjLoadInternal 的 taken 分支直接 goto LOS_END 返回原句柄，
 *        未做 refCnt++（对照 ET_SO 路径 los_ld_dllib.c LD_DlNeededExtract 的 taken 分支
 *        有 LD_DlAdd2NeededDlList + LD_DlRefCntInc，行为不对称）。
 * 【修复方向】taken+IS_LOADED 分支对 moduleHandle->refCnt 递增后再返回。
 * 【恢复方法】KERNEL_BUG_16_DYNLOAD_REL_REFCNT 置 1（bug_isolate.h），本用例恢复注册
 *        常跑；上游修复后以本用例为回归验收。
 * 【关联】MAINTEST_1194/问题记录/20261020_002200；看护 !1194。
 */

extern const UINT8 _binary_reloc_o_start[];
extern const UINT8 _binary_reloc_o_end[];

/* 用例简要描述: ET_REL 同名二次加载引用计数与句柄存续（缺陷看护） */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    VOID *sameHandle = NULL;
    INT32 *(*fnTargetAddr)(VOID) = NULL;
    INT32 ret;

    handle = LOS_MemLoad("refcnt_mem", strlen("refcnt_mem"), (const CHAR *)_binary_reloc_o_start,
                         (UINT32)(_binary_reloc_o_end - _binary_reloc_o_start));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);

    /* 同名二次加载返回同一句柄（修复后契约：且引用计数递增） */
    sameHandle = LOS_MemLoad("refcnt_mem", strlen("refcnt_mem"), (const CHAR *)_binary_reloc_o_start,
                             (UINT32)(_binary_reloc_o_end - _binary_reloc_o_start));
    ICUNIT_GOTO_EQUAL(sameHandle, handle, sameHandle, EXIT);

    /* 卸载一次后模块仍存活、句柄仍可查符号（引用计数语义，修复后契约） */
    ret = LOS_ModuleUnload(sameHandle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    sameHandle = NULL;
    fnTargetAddr = (INT32 *(*)(VOID))LOS_FindSymByName(handle, "reloc_target_addr");
    ICUNIT_GOTO_NOT_EQUAL(fnTargetAddr, NULL, fnTargetAddr, EXIT);

    /* 第二次卸载才真正释放 */
    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    return LOS_OK;

EXIT:
    if (sameHandle != NULL) {
        (VOID)LOS_ModuleUnload(sameHandle);
    }
    if (handle != NULL) {
        (VOID)LOS_ModuleUnload(handle);
    }
    return LOS_OK;
}

VOID ItLosDynload018(VOID)
{
    TEST_ADD_CASE("ItLosDynload018", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

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
 * 【现象】LOS_MemLoad 加载含 SHN_COMMON 符号（gcc -fcommon 下未初始化全局变量）
 *        的 ET_REL 对象时，板级 Load access fault 复位（mepc=LD_ElfSymTypeGet，
 *        寄存器 a3=0xfff2=SHN_COMMON，野指针 0xcf3f10）。
 * 【根因】los_ld_commlib.c:72 LD_ElfSymTypeGet 仅特判 SHN_ABS，对 SHN_COMMON 等
 *        保留索引（>=SHN_LORESERVE=0xff00）直接 scnHdrTbl + stShndx 越界索引
 *        （0xfff2*40≈6.5MB 偏移），无错误码返回路径。
 * 【修复方向】保留索引统一处理：SHN_COMMON→按 BSS 类符号分配（配合 elflib.c 的
 *        LD_SHN_COMMON 分配分支）或返回 LD_SYM_UNDF 拒绝；SHN_UNDEF 等同理。
 * 【恢复方法】KERNEL_BUG_14_DYNLOAD_SHN_COMMON 置 1（bug_isolate.h），本用例恢复
 *        注册常跑；上游修复后以本用例为回归验收。
 * 【关联】MAINTEST_1194/问题记录/20261020_002000；看护 !1194。
 */

extern const UINT8 _binary_relocc_o_start[];
extern const UINT8 _binary_relocc_o_end[];

/* 用例简要描述: 含 SHN_COMMON 符号对象加载与 COMMON 变量分配（缺陷看护） */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    INT32 (*fnCommonInc)(VOID) = NULL;
    INT32 ret;

    /* relocc.o 带 SHN_COMMON 符号 g_rel_common（-DRELOC_WITH_COMMON 编译） */
    handle = LOS_MemLoad("relocc_mem", strlen("relocc_mem"), (const CHAR *)_binary_relocc_o_start,
                         (UINT32)(_binary_relocc_o_end - _binary_relocc_o_start));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);

    IT_DYNLOAD_WX_ON();

    /* COMMON 变量被分配到模块数据区并可读写（修复后契约） */
    fnCommonInc = (INT32 (*)(VOID))LOS_FindSymByName(handle, "common_inc");
    ICUNIT_GOTO_NOT_EQUAL(fnCommonInc, NULL, fnCommonInc, EXIT);
    ret = fnCommonInc();
    ICUNIT_GOTO_EQUAL(ret, 1, ret, EXIT);
    ret = fnCommonInc();
    ICUNIT_GOTO_EQUAL(ret, 2, ret, EXIT);

    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;

    return LOS_OK;

EXIT:
    if (handle != NULL) {
        (VOID)LOS_ModuleUnload(handle);
    }
    return LOS_OK;
}

VOID ItLosDynload016(VOID)
{
    TEST_ADD_CASE("ItLosDynload016", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

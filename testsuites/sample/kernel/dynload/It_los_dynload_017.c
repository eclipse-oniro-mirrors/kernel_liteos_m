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
 * 【现象】加载含 R_RISCV_PCREL_HI20+PCREL_LO12_I(/S) 重定位对的 ET_REL 对象后，
 *        pcrel 取址/存储结果发生页内偏移（ItLosDynload013 轮 7 实测 fnPcrelGet()
 *        返回 0xa74354 ≠ &g_rel_target），pcrel_store 写入错误地址。
 * 【根因】arch/risc-v/common/dynload.c ELF_RiscvPcrelLow12IReloc/Low12SReloc 直接
 *        `offset = symAddr + addend - position`——PCREL_LO12 的 sym 是 AUIPC 处的
 *        局部标签（如 .L1^B1，值为 AUIPC 地址），按 RISC-V psABI 应回溯该 AUIPC 处
 *        PCREL_HI20 的目标符号与 offset 计算低 12 位；现实现得到 offset=标签-本指令
 *        （如 -4），lo12 系统性错误。
 * 【修复方向】PCREL_LO12 处理器按规范实现：定位 sym（AUIPC 标签）处的 PCREL_HI20
 *        重定位条目，取其 (目标符号+addend−AUIPC地址) 的低 12 位写入指令立即数。
 * 【恢复方法】KERNEL_BUG_15_DYNLOAD_PCREL_LO12 置 1（bug_isolate.h），本用例恢复
 *        注册常跑；上游修复后以本用例为回归验收。
 * 【关联】MAINTEST_1194/问题记录/20261020_002100；看护 !1194。
 */

extern const UINT8 _binary_reloc_o_start[];
extern const UINT8 _binary_reloc_o_end[];

/* 用例简要描述: PCREL_HI20+PCREL_LO12 重定位对地址计算（缺陷看护） */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    INT32 *(*fnPcrelGet)(VOID) = NULL;
    VOID (*fnPcrelStore)(INT32) = NULL;
    INT32 *(*fnTargetAddr)(VOID) = NULL;
    INT32 (*fnGetStore)(VOID) = NULL;
    UINTPTR targetAddr;
    INT32 ret;

    handle = LOS_MemLoad("pcrel_mem", strlen("pcrel_mem"), (const CHAR *)_binary_reloc_o_start,
                         (UINT32)(_binary_reloc_o_end - _binary_reloc_o_start));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);

    IT_DYNLOAD_WX_ON();

    fnTargetAddr = (INT32 *(*)(VOID))LOS_FindSymByName(handle, "reloc_target_addr");
    ICUNIT_GOTO_NOT_EQUAL(fnTargetAddr, NULL, fnTargetAddr, EXIT);
    targetAddr = (UINTPTR)fnTargetAddr();

    /* PCREL_HI20 + PCREL_LO12_I 取址必须精确等于目标符号地址（修复后契约） */
    fnPcrelGet = (INT32 *(*)(VOID))LOS_FindSymByName(handle, "pcrel_get_target");
    ICUNIT_GOTO_NOT_EQUAL(fnPcrelGet, NULL, fnPcrelGet, EXIT);
    ICUNIT_GOTO_EQUAL((UINTPTR)fnPcrelGet(), targetAddr, (UINTPTR)fnPcrelGet(), EXIT);

    /* PCREL_HI20 + PCREL_LO12_S 存储必须写到目标符号（修复后契约） */
    fnPcrelStore = (VOID (*)(INT32))LOS_FindSymByName(handle, "pcrel_store");
    ICUNIT_GOTO_NOT_EQUAL(fnPcrelStore, NULL, fnPcrelStore, EXIT);
    fnGetStore = (INT32 (*)(VOID))LOS_FindSymByName(handle, "get_rel_store");
    ICUNIT_GOTO_NOT_EQUAL(fnGetStore, NULL, fnGetStore, EXIT);
    fnPcrelStore(5);
    ret = fnGetStore();
    ICUNIT_GOTO_EQUAL(ret, 5, ret, EXIT);

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

VOID ItLosDynload017(VOID)
{
    TEST_ADD_CASE("ItLosDynload017", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

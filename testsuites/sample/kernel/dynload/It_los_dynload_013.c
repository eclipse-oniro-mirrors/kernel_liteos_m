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

extern const UINT8 _binary_reloc_o_start[];
extern const UINT8 _binary_reloc_o_end[];

/* 内核侧重定位存储目标（It_los_dynload_dso.c 定义并经 OS 符号表导出） */
extern INT32 g_dynload_krel_branch_hit;
extern INT32 g_dynload_krel_jal_hit;
extern INT32 g_dynload_krel_store;

/* 用例简要描述: ObjLoad 重定位类型全覆盖与 RELATIVE 指针校正 */
STATIC UINT32 TestCase(VOID)
{
    VOID *handle = NULL;
    VOID *soHandle = NULL;
    VOID *badHandle = NULL;
    INT32 (*fnA)(INT32, INT32) = NULL;
    INT32 (*fnDeref)(VOID) = NULL;
    VOID (*fnLo12sStore)(INT32) = NULL;
    VOID (*fnJal)(VOID) = NULL;
    VOID (*fnBranch)(VOID) = NULL;
    unsigned char (*fnAdd8)(VOID) = NULL;
    unsigned short (*fnAdd16)(VOID) = NULL;
    unsigned int (*fnAdd32)(VOID) = NULL;
    unsigned char (*fnSub8)(VOID) = NULL;
    unsigned short (*fnSub16)(VOID) = NULL;
    unsigned int (*fnSub32)(VOID) = NULL;
    INT32 *(*fnTargetAddr)(VOID) = NULL;
    UINTPTR targetAddr;
    INT32 ret;

    /* ET_REL：reloc.o 覆盖 LO12_S/JAL/BRANCH/ADD/SUB 重定位族（PCREL 族见 It_los_dynload_017） */
    handle = LOS_MemLoad("reloc_mem", strlen("reloc_mem"), (const CHAR *)_binary_reloc_o_start,
                         (UINT32)(_binary_reloc_o_end - _binary_reloc_o_start));
    ICUNIT_GOTO_NOT_EQUAL(handle, NULL, handle, EXIT);

    /* 不存在的可重定位文件被拒绝（同名二次加载的引用计数语义见 It_los_dynload_018） */
    badHandle = LOS_ObjLoad(DSO_FULL_PATH("no_such_reloc.o"));
    ICUNIT_GOTO_EQUAL(badHandle, NULL, badHandle, EXIT);

    /* W^X 板：加载后窗口为 RW，必须在首次调用模块代码前切 RX */
    IT_DYNLOAD_WX_ON();

    fnTargetAddr = (INT32 *(*)(VOID))LOS_FindSymByName(handle, "reloc_target_addr");
    ICUNIT_GOTO_NOT_EQUAL(fnTargetAddr, NULL, fnTargetAddr, EXIT);
    targetAddr = (UINTPTR)fnTargetAddr();

    fnLo12sStore = (VOID (*)(INT32))LOS_FindSymByName(handle, "lo12s_store");
    ICUNIT_GOTO_NOT_EQUAL(fnLo12sStore, NULL, fnLo12sStore, EXIT);
    fnJal = (VOID (*)(VOID))LOS_FindSymByName(handle, "jal_call");
    ICUNIT_GOTO_NOT_EQUAL(fnJal, NULL, fnJal, EXIT);
    fnJal();
    ICUNIT_GOTO_EQUAL(g_dynload_krel_jal_hit, 1, g_dynload_krel_jal_hit, EXIT);

    fnBranch = (VOID (*)(VOID))LOS_FindSymByName(handle, "branch_call");
    ICUNIT_GOTO_NOT_EQUAL(fnBranch, NULL, fnBranch, EXIT);
    fnBranch();
    ICUNIT_GOTO_EQUAL(g_dynload_krel_branch_hit, 1, g_dynload_krel_branch_hit, EXIT);

    fnAdd8 = (unsigned char (*)(VOID))LOS_FindSymByName(handle, "add8_get");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd8, NULL, fnAdd8, EXIT);
    fnAdd16 = (unsigned short (*)(VOID))LOS_FindSymByName(handle, "add16_get");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd16, NULL, fnAdd16, EXIT);
    fnAdd32 = (unsigned int (*)(VOID))LOS_FindSymByName(handle, "add32_get");
    ICUNIT_GOTO_NOT_EQUAL(fnAdd32, NULL, fnAdd32, EXIT);
    fnSub8 = (unsigned char (*)(VOID))LOS_FindSymByName(handle, "sub8_get");
    ICUNIT_GOTO_NOT_EQUAL(fnSub8, NULL, fnSub8, EXIT);
    fnSub16 = (unsigned short (*)(VOID))LOS_FindSymByName(handle, "sub16_get");
    ICUNIT_GOTO_NOT_EQUAL(fnSub16, NULL, fnSub16, EXIT);
    fnSub32 = (unsigned int (*)(VOID))LOS_FindSymByName(handle, "sub32_get");
    ICUNIT_GOTO_NOT_EQUAL(fnSub32, NULL, fnSub32, EXIT);

    /* ADD 族：字长截断后的符号地址；SUB 族：0 - 截断符号地址 */
    ICUNIT_GOTO_EQUAL(fnAdd8(), (unsigned char)targetAddr, fnAdd8(), EXIT);
    ICUNIT_GOTO_EQUAL(fnAdd16(), (unsigned short)targetAddr, fnAdd16(), EXIT);
    ICUNIT_GOTO_EQUAL(fnAdd32(), (unsigned int)targetAddr, fnAdd32(), EXIT);
    ICUNIT_GOTO_EQUAL(fnSub8(), (unsigned char)(0 - (unsigned int)targetAddr), fnSub8(), EXIT);
    ICUNIT_GOTO_EQUAL(fnSub16(), (unsigned short)(0 - (unsigned int)targetAddr), fnSub16(), EXIT);
    ICUNIT_GOTO_EQUAL(fnSub32(), (unsigned int)(0 - (unsigned int)targetAddr), fnSub32(), EXIT);

    /* LO12_S 存储路径：写入目标为内核侧导出符号（W^X 板模块数据不可写） */
    /* PCREL_LO12_S 断言见 It_los_dynload_017（KERNEL_BUG_15 隔离） */
    fnLo12sStore(9);
    ICUNIT_GOTO_EQUAL(g_dynload_krel_store, 9, g_dynload_krel_store, EXIT);

    /* ET_DYN：basic.so 内静态指针生成 R_RISCV_RELATIVE，加载后指向模块自身数据；
     * 经解引用读取初值验证（写无关，兼容 W^X 板） */
    soHandle = LOS_SoLoad(DSO_FULL_PATH("basic.so"));
    ICUNIT_GOTO_NOT_EQUAL(soHandle, NULL, soHandle, EXIT);
    IT_DYNLOAD_WX_ON(); /* W^X 板：本次加载已将窗口翻回 RW，调用前重新切 RX */
    fnDeref = (INT32 (*)(VOID))LOS_FindSymByName(soHandle, "rel_ptr_deref");
    ICUNIT_GOTO_NOT_EQUAL(fnDeref, NULL, fnDeref, EXIT);
    ret = fnDeref();
    ICUNIT_GOTO_EQUAL(ret, 5, ret, EXIT);
    fnA = (INT32 (*)(INT32, INT32))LOS_FindSymByName(soHandle, "add_basic");
    ICUNIT_GOTO_NOT_EQUAL(fnA, NULL, fnA, EXIT);
    ret = fnA(20, 22);
    ICUNIT_GOTO_EQUAL(ret, 142, ret, EXIT);

    ret = LOS_ModuleUnload(handle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    handle = NULL;
    ret = LOS_ModuleUnload(soHandle);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    soHandle = NULL;

    return LOS_OK;

EXIT:
    if (handle != NULL) {
        (VOID)LOS_ModuleUnload(handle);
    }
    if (soHandle != NULL) {
        (VOID)LOS_ModuleUnload(soHandle);
    }
    return LOS_OK;
}

VOID ItLosDynload013(VOID)
{
    TEST_ADD_CASE("ItLosDynload013", TestCase, TEST_LOS, TEST_DYNLOAD, TEST_LEVEL1, TEST_FUNCTION);
}

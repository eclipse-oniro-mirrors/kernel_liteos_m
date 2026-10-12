/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_mem.h"

#ifdef LOSCFG_KERNEL_MEM_TLSF

/* 池外对齐野指针(仅参与 OsMemAddrValidCheck 的范围比较, 不被解引用) */
#define MEM_TC321_FOREIGN_ADDR   0xC0000000U

/* 用例简要描述: prev越界free返回NOK不崩溃可恢复 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    VOID *p1 = NULL;
    VOID *p2 = NULL;
    struct TestMemNodeHead *node1 = NULL;
    struct TestMemNodeHead *prevBackup = NULL;
    UINT32 corrupted = 0;

    MemStart();

    MemInit();

    /* F-State: 相邻两块 p1/p2 */
    p1 = LOS_MemAlloc(g_memPool, 0x20);
    ICUNIT_GOTO_NOT_EQUAL(p1, NULL, 0, EXIT);
    p2 = LOS_MemAlloc(g_memPool, 0x20);
    ICUNIT_GOTO_NOT_EQUAL(p2, NULL, 0, EXIT);

    /* free p1 使其成为空闲节点(保留在空闲链上) */
    ret = LOS_MemFree(g_memPool, p1);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- fuzz: 保存 -> 破坏 p1 空闲节点头 ptr.prev 为池外对齐地址 ---- */
    node1 = (struct TestMemNodeHead *)((UINTPTR)p1 - sizeof(struct TestMemNodeHead));
    prevBackup = node1->ptr.prev;
    corrupted = 1;
    node1->ptr.prev = (struct TestMemNodeHead *)MEM_TC321_FOREIGN_ADDR;

    /* free 相邻后继 p2: OsMemCheckUsedNode 校验前驱(p1, 空闲)时
     * OsMemIsNodeValid -> OsMemAddrValidCheck(ptr.prev 越界) 返回 FALSE(1112),
     * OsMemFree 打印后返回 LOS_NOK, 不 panic(1176-1178) */
    ret = LOS_MemFree(g_memPool, p2);
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);
    if (ret == LOS_OK) { /* 防御: 非预期成功则避免 EXIT 双重释放 */
        p2 = NULL;
    }

    /* ---- 恢复 -> 断言功能正常 ---- */
    node1->ptr.prev = prevBackup;
    corrupted = 0;

    ret = LOS_MemFree(g_memPool, p2);
    if (ret == LOS_OK) {
        p2 = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    p1 = NULL;

    ret = LOS_MemIntegrityCheck(g_memPool);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    if (corrupted == 1) {
        /* 破坏态下断言失败跳转至此: 先恢复原值再返回, 保证池不被污染 */
        node1->ptr.prev = prevBackup;
    }
    if (p2 != NULL) {
        (VOID)LOS_MemFree(g_memPool, p2);
    }
    MemFree();
    MemEnd();
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_TLSF */

VOID ItLosMem321(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_TLSF
    TEST_ADD_CASE("ItLosMem321", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

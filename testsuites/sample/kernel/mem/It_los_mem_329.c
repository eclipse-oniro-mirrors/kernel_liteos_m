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

/* dlink 头检查分支要求 LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK(bestfit 1073-1118) */
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK == 1)

#define MEM_TC329_POOL_SIZE     0x1000
/* 池外对齐野指针(仅参与 OsMemPoolDlinkcheck 的范围比较, 不被解引用) */
#define MEM_TC329_FOREIGN_ADDR  0xC0000000U

STATIC UINT8 g_memPoolTC329[MEM_TC329_POOL_SIZE] __attribute__((aligned(64)));

/* 用例简要描述: dlink头破坏后完整性检查仅打印不崩 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 size;
    VOID *ptr = NULL;
    LosMultipleDlinkHead *dlink = NULL;
    LOS_DL_LIST *list0 = NULL;
    LOS_DL_LIST *prevBackup = NULL;
    UINT32 corrupted = 0;

    ret = LOS_MemPoolInit(g_memPoolTC329, MEM_TC329_POOL_SIZE, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* dlink 头数组位于 pool + sizeof(LosMemPoolInfo)(bestfit OS_MEM_HEAD_ADDR);
     * 破坏 listHead[0].pstPrev 为池外对齐地址 */
    dlink = (LosMultipleDlinkHead *)(VOID *)((UINTPTR)g_memPoolTC329 + sizeof(TestMemPoolInfoBF));
    list0 = &dlink->listHead[0];

    /* ---- fuzz: 保存 -> 破坏 -> 断言安全失败(仅打印) ---- */
    prevBackup = list0->pstPrev;
    corrupted = 1;
    list0->pstPrev = (LOS_DL_LIST *)MEM_TC329_FOREIGN_ADDR;

    /* LOS_MemIntegrityCheck -> OsMemPoolHeadInfoPrint -> OsMemPoolDlinkcheck 失败
     * 仅打印 dlink 头信息(bestfit 1106-1118), 节点遍历不受影响, 返回 LOS_OK */
    ret = LOS_MemIntegrityCheck(g_memPoolTC329);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* ---- 恢复 -> 断言功能正常 ---- */
    list0->pstPrev = prevBackup;
    corrupted = 0;

    ret = LOS_MemIntegrityCheck(g_memPoolTC329);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 池可继续 alloc/free */
    size = 0x40;
    ptr = LOS_MemAlloc(g_memPoolTC329, size);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);
    ret = LOS_MemFree(g_memPoolTC329, ptr);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ptr = NULL;

    ret = LOS_MemDeInit(g_memPoolTC329);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    if (corrupted == 1) {
        /* 破坏态下断言失败跳转至此: 先恢复原值再返回, 保证池不被污染 */
        list0->pstPrev = prevBackup;
    }
    if (ptr != NULL) {
        (VOID)LOS_MemFree(g_memPoolTC329, ptr);
        ptr = NULL;
    }
    (VOID)LOS_MemDeInit(g_memPoolTC329);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_BESTFIT && LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK */

VOID ItLosMem329(VOID)
{
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (LOSCFG_BASE_MEM_NODE_INTEGRITY_CHECK == 1)
    TEST_ADD_CASE("ItLosMem329", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

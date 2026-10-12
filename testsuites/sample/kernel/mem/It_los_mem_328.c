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

/* 尾节点 magic 校验分支要求 LOSCFG_MEM_DEBUG 或 LOSCFG_MEM_TASK_STAT(bestfit 1291-1299) */
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (defined(LOSCFG_MEM_DEBUG) || defined(LOSCFG_MEM_TASK_STAT))

#define MEM_TC328_POOL_SIZE     0x1000

STATIC UINT8 g_memPoolTC328[MEM_TC328_POOL_SIZE] __attribute__((aligned(64)));

/* 用例简要描述: 尾节点magic破坏后InfoGet返回NOK */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 poolNumBefore;
    UINT32 poolNumDuring;
    UINTPTR magicBackup = 0;
    UINT32 corrupted = 0;
    LOS_MEM_POOL_STATUS poolStatus = {0};
    TestMemDynNodeBF *endNode = NULL;

    ret = LOS_MemPoolInit(g_memPoolTC328, MEM_TC328_POOL_SIZE, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* END 节点 = pool + poolSize - 节点头大小(白盒偏移, 结构副本见 It_los_mem.h) */
    endNode = (TestMemDynNodeBF *)(VOID *)((UINTPTR)g_memPoolTC328 +
        LOS_MemPoolSizeGet(g_memPoolTC328) - TEST_BF_NODE_HEAD_SIZE);

    poolNumBefore = LOS_MemPoolList();

    /* ---- fuzz: 保存 -> 破坏 END 节点 magic -> 断言安全失败 ---- */
    magicBackup = endNode->selfNode.magic;
    corrupted = 1;
    endNode->selfNode.magic = 0; /* 破坏地址异或 magic */

    /* OsMemInfoGet 尾节点 magic 校验失败 -> PRINT_ERR + LOS_NOK(bestfit 1295-1297) */
    ret = LOS_MemInfoGet(g_memPoolTC328, &poolStatus);
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    /* 破坏窗口内 LOS_MemPoolList: OsMemInfoPrint 对坏池早退(1332-1333),
     * 池链遍历不受影响, 返回池数不变 */
    poolNumDuring = LOS_MemPoolList();
    ICUNIT_GOTO_EQUAL(poolNumDuring, poolNumBefore, poolNumDuring, EXIT);

    /* ---- 恢复 -> 断言功能正常 ---- */
    endNode->selfNode.magic = magicBackup;
    corrupted = 0;

    ret = LOS_MemInfoGet(g_memPoolTC328, &poolStatus);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemDeInit(g_memPoolTC328);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    if (corrupted == 1) {
        /* 破坏态下断言失败跳转至此: 先恢复原值再返回, 保证池不被污染 */
        endNode->selfNode.magic = magicBackup;
    }
    (VOID)LOS_MemDeInit(g_memPoolTC328);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_BESTFIT && (MEM_DEBUG || MEM_TASK_STAT) */

VOID ItLosMem328(VOID)
{
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (defined(LOSCFG_MEM_DEBUG) || defined(LOSCFG_MEM_TASK_STAT))
    TEST_ADD_CASE("ItLosMem328", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

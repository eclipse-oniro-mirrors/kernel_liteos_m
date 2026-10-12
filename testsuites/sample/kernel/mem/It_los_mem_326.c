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

#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (LOSCFG_MEM_MUL_MODULE == 1)

#define MEM_TC326_POOL_SIZE     0x1000
#define MEM_TC326_MOD_ID        1

/* bestfit 节点 gap/sizeAndFlag 标志位(bestfit/los_memory_internal.h:91-93) */
#define MEM_TC326_USED_FLAG     0x80000000U
#define MEM_TC326_ALIGNED_FLAG  0x40000000U

STATIC UINT8 g_memPoolTC326[MEM_TC326_POOL_SIZE] __attribute__((aligned(64)));

/* 用例简要描述: 坏gap字Mfree返回NOK恢复后正常 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 wordBackup = 0;
    UINT32 corrupted = 0;
    VOID *ptr = NULL;

    ret = LOS_MemPoolInit(g_memPoolTC326, MEM_TC326_POOL_SIZE, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 普通分配: ptr-4 即节点头 sizeAndFlag 字(USED 置位, ALIGNED 清零) */
    ptr = LOS_MemMalloc(g_memPoolTC326, 0x20, MEM_TC326_MOD_ID);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);

    /* ---- fuzz: 保存原值 -> 破坏 -> 断言安全失败 ---- */
    wordBackup = *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32)));

    /* 坏值 1: USED|ALIGNED 双标志 -> OsMemPtrToNode 打印+NULL(2294-2295)
     * -> LOS_MemMfree 返回 LOS_NOK */
    corrupted = 1;
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = MEM_TC326_USED_FLAG | MEM_TC326_ALIGNED_FLAG;
    ret = LOS_MemMfree(g_memPoolTC326, ptr, MEM_TC326_MOD_ID);
    if (ret == LOS_OK) { /* 防御: 非预期成功则避免 EXIT 双重释放 */
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    /* 坏值 2: ALIGNED|6(解码 gapSize=6 未按 OS_MEM_ALIGN_SIZE 对齐) -> 2299-2301 */
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = MEM_TC326_ALIGNED_FLAG | 6;
    ret = LOS_MemMfree(g_memPoolTC326, ptr, MEM_TC326_MOD_ID);
    if (ret == LOS_OK) { /* 防御: 非预期成功则避免 EXIT 双重释放 */
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    /* ---- 恢复 -> 断言功能正常 ---- */
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = wordBackup;
    corrupted = 0;
    ret = LOS_MemMfree(g_memPoolTC326, ptr, MEM_TC326_MOD_ID);
    if (ret == LOS_OK) {
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemDeInit(g_memPoolTC326);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    if ((corrupted == 1) && (ptr != NULL)) {
        /* 破坏态下断言失败跳转至此: 先恢复原值再返回, 保证池不被污染 */
        *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = wordBackup;
    }
    if (ptr != NULL) {
        (VOID)LOS_MemMfree(g_memPoolTC326, ptr, MEM_TC326_MOD_ID);
        ptr = NULL;
    }
    (VOID)LOS_MemDeInit(g_memPoolTC326);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_BESTFIT && LOSCFG_MEM_MUL_MODULE */

VOID ItLosMem326(VOID)
{
#if defined(LOSCFG_KERNEL_MEM_BESTFIT) && (LOSCFG_MEM_MUL_MODULE == 1)
    TEST_ADD_CASE("ItLosMem326", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

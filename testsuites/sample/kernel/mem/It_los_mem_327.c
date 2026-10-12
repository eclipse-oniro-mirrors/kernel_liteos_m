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

#ifdef LOSCFG_KERNEL_MEM_BESTFIT

#define MEM_TC327_POOL_SIZE     0x1000
#define MEM_TC327_ALIGN         0x40
#define MEM_TC327_ALLOC         0x20
#define MEM_TC327_TRY_MAX       16

/* bestfit gap 字标志位(bestfit/los_memory_internal.h:91-93) */
#define MEM_TC327_USED_FLAG     0x80000000U
#define MEM_TC327_ALIGNED_FLAG  0x40000000U

STATIC UINT8 g_memPoolTC327[MEM_TC327_POOL_SIZE] __attribute__((aligned(64)));

/* 用例简要描述: 双标志gap字Free返回NOK可恢复 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 gapWord = 0;
    UINT32 gapBackup = 0;
    UINT32 corrupted = 0;
    VOID *ptr = NULL;
    VOID *keep[MEM_TC327_TRY_MAX] = {NULL};
    INT32 try = 0;
    INT32 keepCnt = 0;
    INT32 i;

    ret = LOS_MemPoolInit(g_memPoolTC327, MEM_TC327_POOL_SIZE, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 前置: 分配到带 gap 字的对齐块(gap 字= alignedPtr-4 处 ALIGNED|gapSize);
     * 若本次 AllocAlign 天然对齐无 gap 字, 保留该块换相位重试(有界 16 次) */
    for (try = 0; try < MEM_TC327_TRY_MAX; try++) {
        ptr = LOS_MemAllocAlign(g_memPoolTC327, MEM_TC327_ALLOC, MEM_TC327_ALIGN);
        ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);
        keep[keepCnt++] = ptr;
        gapWord = *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32)));
        if ((gapWord & MEM_TC327_ALIGNED_FLAG) && !(gapWord & MEM_TC327_USED_FLAG)) {
            break;
        }
        ptr = NULL;
    }
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);

    /* ---- fuzz: 保存原值 -> 破坏 -> 断言安全失败 ---- */
    gapBackup = gapWord;

    /* 坏值: USED|ALIGNED 双标志 -> OsGetRealPtr 打印+NULL(bestfit 1718-1719)
     * -> LOS_MemFree 返回 LOS_NOK */
    corrupted = 1;
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = MEM_TC327_USED_FLAG | MEM_TC327_ALIGNED_FLAG;
    ret = LOS_MemFree(g_memPoolTC327, ptr);
    if (ret == LOS_OK) { /* 防御: 非预期成功则避免 EXIT 双重释放 */
        keep[keepCnt - 1] = NULL;
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    /* ---- 恢复 -> 断言功能正常 ---- */
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = gapBackup;
    corrupted = 0;
    ret = LOS_MemFree(g_memPoolTC327, ptr);
    if (ret == LOS_OK) {
        keep[keepCnt - 1] = NULL;
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemIntegrityCheck(g_memPoolTC327);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemDeInit(g_memPoolTC327);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    if ((corrupted == 1) && (ptr != NULL)) {
        /* 破坏态下断言失败跳转至此: 先恢复原值再返回, 保证池不被污染 */
        *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = gapBackup;
    }
    for (i = 0; i < keepCnt; i++) {
        if (keep[i] != NULL) {
            (VOID)LOS_MemFree(g_memPoolTC327, keep[i]);
            keep[i] = NULL;
        }
    }
    (VOID)LOS_MemDeInit(g_memPoolTC327);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_BESTFIT */

VOID ItLosMem327(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    TEST_ADD_CASE("ItLosMem327", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

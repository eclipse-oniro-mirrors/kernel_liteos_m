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

/* tlsf gap 字标志位(los_memory.c:155-167): ALIGNED=0x40000000, USED=0x80000000 */
#define MEM_TC320_GAP_USED_FLAG        0x80000000U
#define MEM_TC320_GAP_ALIGNED_FLAG     0x40000000U
#define MEM_TC320_TRY_MAX              16

/* 用例简要描述: 坏gap字Free安全失败且可恢复 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 alignSize = 0x40;
    UINT32 allocSize = 0x20;
    UINT32 gapWord = 0;
    UINT32 gapBackup = 0;
    UINT32 corrupted = 0;
    UINT32 badGap[3] = { /* 3, bad gap words: USED|ALIGNED / ALIGNED|未对齐6 / 超大对齐值 */
        MEM_TC320_GAP_USED_FLAG | MEM_TC320_GAP_ALIGNED_FLAG,
        MEM_TC320_GAP_ALIGNED_FLAG | 6,
        MEM_TC320_GAP_ALIGNED_FLAG | 0xFFF0
    };
    VOID *ptr = NULL;
    VOID *keep[MEM_TC320_TRY_MAX] = {NULL};
    INT32 i;
    INT32 try = 0;
    INT32 keepCnt = 0;

    MemStart();

    MemInit();

    /* 前置: 分配到带 gap 字的对齐块(gap 字= alignedPtr-4 处 0x40000000|gapSize);
     * 若本次 AllocAlign 天然对齐无 gap 字, 保留该块换相位重试(有界 16 次) */
    for (try = 0; try < MEM_TC320_TRY_MAX; try++) {
        ptr = LOS_MemAllocAlign(g_memPool, allocSize, alignSize);
        ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);
        keep[keepCnt++] = ptr;
        gapWord = *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32)));
        if ((gapWord & MEM_TC320_GAP_ALIGNED_FLAG) && !(gapWord & MEM_TC320_GAP_USED_FLAG)) {
            break;
        }
        ptr = NULL;
    }
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);

    /* ---- fuzz: 保存原值 -> 破坏 -> 断言安全失败 ---- */
    gapBackup = gapWord;

    /* 坏值 1: USED|ALIGNED 双标志 -> OsGetRealPtr 打印+NULL(1236-1237) */
    corrupted = 1;
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = badGap[0];
    ret = LOS_MemFree(g_memPool, ptr);
    if (ret == LOS_OK) { /* 防御: 非预期成功则避免 EXIT 双重释放 */
        keep[keepCnt - 1] = NULL;
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    /* 坏值 2: ALIGNED|6(gapSize 未按 OS_MEM_ALIGN_SIZE 对齐) -> 1244-1245 */
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = badGap[1];
    ret = LOS_MemFree(g_memPool, ptr);
    if (ret == LOS_OK) { /* 防御: 非预期成功则避免 EXIT 双重释放 */
        keep[keepCnt - 1] = NULL;
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    /* 坏值 3: ALIGNED|0xFFF0(gapSize 超出 ptr 相对池首偏移) -> 1244-1245 */
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = badGap[2];
    ret = LOS_MemFree(g_memPool, ptr);
    if (ret == LOS_OK) { /* 防御: 非预期成功则避免 EXIT 双重释放 */
        keep[keepCnt - 1] = NULL;
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_NOK, ret, EXIT);

    /* ---- 恢复 -> 断言功能正常 ---- */
    *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = gapBackup;
    corrupted = 0;
    ret = LOS_MemFree(g_memPool, ptr);
    if (ret == LOS_OK) {
        keep[keepCnt - 1] = NULL;
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemIntegrityCheck(g_memPool);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    if ((corrupted == 1) && (ptr != NULL)) {
        /* 破坏态下断言失败跳转至此: 先恢复原值再返回, 保证池不被污染 */
        *((UINT32 *)((UINTPTR)ptr - sizeof(UINT32))) = gapBackup;
    }
    for (i = 0; i < keepCnt; i++) {
        if (keep[i] != NULL) {
            (VOID)LOS_MemFree(g_memPool, keep[i]);
            keep[i] = NULL;
        }
    }
    MemFree();
    MemEnd();
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_TLSF */

VOID ItLosMem320(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_TLSF
    TEST_ADD_CASE("ItLosMem320", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

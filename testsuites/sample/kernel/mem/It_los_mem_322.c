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
#include "los_slab.h"
#include "los_slab_pri.h"

#if (LOSCFG_KERNEL_MEM_SLAB_EXTENTION == 1)

#define MEM_TC322_POOL_SIZE     0x2000
#define MEM_TC322_BLK_CNT       33  /* 33 allocs: first 32 fill bitmap word0, the 33rd crosses the word */
#define MEM_TC322_STRIDE        (16 + 4) /* slab item = blkSz(16) + block node head(4) */

STATIC UINT8 g_memPoolTC322[MEM_TC322_POOL_SIZE] __attribute__((aligned(64)));

/* 用例简要描述: 跨32bit字位图连续33次分配全成功 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT32 cfg[SLAB_MEM_COUNT];
    UINT32 cfgDefault[SLAB_MEM_COUNT];
    VOID *ptr[MEM_TC322_BLK_CNT] = {NULL};

    /* F-Pos 前置: cfg[0]=1024(默认 512) -> class0(blkSz=16) blkCnt=64=2 个 32bit 位图字 */
    for (i = 0; i < SLAB_MEM_COUNT; i++) {
        cfg[i] = SLAB_MEM_ALLOCATOR_SIZE;
    }
    cfg[0] = 1024; /* 1024 / 16 = 64 items = 2 bitmap words */
    LOS_SlabSizeCfg(cfg, SLAB_MEM_COUNT);

    ret = LOS_MemInit(g_memPoolTC322, MEM_TC322_POOL_SIZE);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 连续 33 次 16B 分配, 第 33 次跳过已满的 word0 命中
     * OsAtomicBitsetFindClearAndSet 的跨字 continue 分支(los_slab.c:85) */
    for (i = 0; i < MEM_TC322_BLK_CNT; i++) {
        ptr[i] = LOS_MemAlloc(g_memPoolTC322, 16);
        ICUNIT_GOTO_NOT_EQUAL(ptr[i], NULL, i, EXIT);
    }

    /* 地址步进断言: 同字内从高位向低位按 itemSz=20 步进 */
    for (i = 1; i < 32; i++) {
        ICUNIT_GOTO_EQUAL((UINTPTR)ptr[i - 1] - (UINTPTR)ptr[i], MEM_TC322_STRIDE, i, EXIT);
    }
    /* 第 33 次落在 word1 的最高位, 与首个分配(word0 最高位)相差 20*32 */
    ICUNIT_GOTO_EQUAL((UINTPTR)ptr[32] - (UINTPTR)ptr[0], MEM_TC322_STRIDE * 32, 32, EXIT);

    /* 触碰数据后全部释放成功 */
    for (i = 0; i < MEM_TC322_BLK_CNT; i++) {
        (VOID)memset_s(ptr[i], 16, 0x5A, 16);
    }
    for (i = 0; i < MEM_TC322_BLK_CNT; i++) {
        ret = LOS_MemFree(g_memPoolTC322, ptr[i]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        ptr[i] = NULL;
    }

    ret = LOS_MemDeInit(g_memPoolTC322);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    for (i = 0; i < MEM_TC322_BLK_CNT; i++) {
        if (ptr[i] != NULL) {
            (VOID)LOS_MemFree(g_memPoolTC322, ptr[i]);
            ptr[i] = NULL;
        }
    }
    (VOID)LOS_MemDeInit(g_memPoolTC322);
    /* D1: 出口恢复编译默认 cfg(OsSlabMemInit 末尾 OsSlabSizeReset 亦会复位) */
    for (i = 0; i < SLAB_MEM_COUNT; i++) {
        cfgDefault[i] = SLAB_MEM_ALLOCATOR_SIZE;
    }
    LOS_SlabSizeCfg(cfgDefault, SLAB_MEM_COUNT);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_SLAB_EXTENTION == 1 */

VOID ItLosMem322(VOID)
{
#if (LOSCFG_KERNEL_MEM_SLAB_EXTENTION == 1)
    TEST_ADD_CASE("ItLosMem322", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

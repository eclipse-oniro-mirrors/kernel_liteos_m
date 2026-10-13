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

#define MEM_TC323_POOL_SIZE     0x2000


/* 用例简要描述: slab类分配失败回退bestfit堆分配 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT32 cfg[SLAB_MEM_COUNT];
    UINT32 cfgDefault[SLAB_MEM_COUNT];
    VOID *ptr = NULL;
    VOID *pool = NULL;

    /* 大池动态申请(64 对齐, 与原静态 aligned(64) 语义一致), 避免常驻 .bss
     * 挤占 SRAM 预算(覆盖率构建下 gcov 计数器已占用大量 .bss) */
    pool = LOS_MemAllocAlign(LOSCFG_SYS_HEAP_ADDR, MEM_TC323_POOL_SIZE, 64);
    ICUNIT_GOTO_NOT_EQUAL(pool, NULL, 0, EXIT);

    /* F-Inv 前置: cfg[0]=0x8000(32KB, 远超 0x2000 池容量) -> class0 的 slab 区块
     * OsSlabAllocatorNew 内 OsMemAlloc 失败 -> PRINT_ERR "slab alloc failed"
     * 且 class0 alloc=NULL(los_slabmem.c:312), 池仍可用; 其余类保持默认 */
    for (i = 0; i < SLAB_MEM_COUNT; i++) {
        cfg[i] = SLAB_MEM_ALLOCATOR_SIZE;
    }
    cfg[0] = 0x8000;
    LOS_SlabSizeCfg(cfg, SLAB_MEM_COUNT);

    ret = LOS_MemInit(pool, MEM_TC323_POOL_SIZE);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 16B 小分配 -> class0 alloc==NULL -> OsSlabMemAlloc 返回 NULL
     * -> LOS_MemAlloc 回退 bestfit 堆分配成功(OsSlabMemAlloc:353-355 路径) */
    ptr = LOS_MemAlloc(pool, 16);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);

    (VOID)memset_s(ptr, 16, 0xA5, 16);
    ICUNIT_GOTO_EQUAL(*((UINT8 *)ptr), 0xA5, 0, EXIT);

    ret = LOS_MemFree(pool, ptr);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ptr = NULL;

    /* 回退堆后池持续可用(混合尺寸健康检查) */
    ptr = LOS_MemAlloc(pool, 0x100);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);
    ret = LOS_MemFree(pool, ptr);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ptr = NULL;

    ret = LOS_MemDeInit(pool);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    if (ptr != NULL) {
        (VOID)LOS_MemFree(pool, ptr);
        ptr = NULL;
    }
    (VOID)LOS_MemDeInit(pool);
    if (pool != NULL) {
        (VOID)LOS_MemFree(LOSCFG_SYS_HEAP_ADDR, pool);
        pool = NULL;
    }
    /* D1: 出口恢复编译默认 cfg */
    for (i = 0; i < SLAB_MEM_COUNT; i++) {
        cfgDefault[i] = SLAB_MEM_ALLOCATOR_SIZE;
    }
    LOS_SlabSizeCfg(cfgDefault, SLAB_MEM_COUNT);
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_SLAB_EXTENTION == 1 */

VOID ItLosMem323(VOID)
{
#if (LOSCFG_KERNEL_MEM_SLAB_EXTENTION == 1)
    TEST_ADD_CASE("ItLosMem323", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

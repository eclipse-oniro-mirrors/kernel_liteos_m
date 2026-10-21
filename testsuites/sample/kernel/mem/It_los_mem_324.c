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

#define MEM_TC324_BUF_SIZE      0x2000
#define MEM_TC324_SIZE_UNALIGN  (MEM_TC324_BUF_SIZE - 2) /* 0x1FFE: not 4-aligned */
#define MEM_TC324_SIZE_ALIGNED  (MEM_TC324_BUF_SIZE - 4) /* 0x1FFC: one step down */
#define MEM_TC324_SIZE_LMS      0x1E00 /* LMS active: pool shrunk to 16/17 aligned down to 64 */


/* 用例简要描述: 未对齐size建池下调对齐并生效 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 sizeGet;
    VOID *ptr = NULL;
    VOID *pool = NULL;

    /* 大池动态申请(64 对齐, 与原静态 aligned(64) 语义一致), 避免常驻 .bss
     * 挤占 SRAM 预算(覆盖率构建下 gcov 计数器已占用大量 .bss) */
    pool = LOS_MemAllocAlign(LOSCFG_SYS_HEAP_ADDR, MEM_TC324_BUF_SIZE, 64);
    ICUNIT_GOTO_NOT_EQUAL(pool, NULL, 0, EXIT);

    /* F-BVA: size 未按 OS_MEM_ALIGN_SIZE 对齐(0x2000-2) -> 打印警告并下调一级对齐
     * (bestfit los_memory.c:1565-1566), 建池仍成功 */
    ret = LOS_MemPoolInit(pool, MEM_TC324_SIZE_UNALIGN, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 尺寸断言: 无 LMS 时为下调对齐值 0x1FFC; LMS 生效时池尺寸二次收缩为 0x1E00 */
    sizeGet = LOS_MemPoolSizeGet(pool);
    ret = ((sizeGet == MEM_TC324_SIZE_ALIGNED) || (sizeGet == MEM_TC324_SIZE_LMS)) ? LOS_OK : LOS_NOK;
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, sizeGet, EXIT);

    /* F-Pos: 下调后的池可正常分配/释放 */
    ptr = LOS_MemAlloc(pool, 0x40);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);
    (VOID)memset_s(ptr, 0x40, 0x3C, 0x40);
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
    return LOS_OK;
}

#endif /* LOSCFG_KERNEL_MEM_BESTFIT */

VOID ItLosMem324(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    TEST_ADD_CASE("ItLosMem324", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

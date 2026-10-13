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

#define MEM_TC331_POOL_SIZE     0x1800

/* 用例简要描述: 大分配吃满池后LastUsedGet返回精确值 */
static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT32 poolSize;
    UINT32 firstNodeSize;
    UINT32 requestSize;
    UINTPTR lastUsed;
    VOID *ptr = NULL;
    VOID *pool = NULL;

    /* 大池动态申请(64 对齐, 与原静态 aligned(64) 语义一致), 避免常驻 .bss
     * 挤占 SRAM 预算(覆盖率构建下 gcov 计数器已占用大量 .bss) */
    pool = LOS_MemAllocAlign(LOSCFG_SYS_HEAP_ADDR, MEM_TC331_POOL_SIZE, 64);
    ICUNIT_GOTO_NOT_EQUAL(pool, NULL, 0, EXIT);

    /* F-Pos: 新池一次大分配吃满首节点: 首空闲节点 =
     * poolSize - sizeof(LosMemPoolInfo) - OS_DLNK_HEAD_SIZE - 节点头(白盒偏移) */
    ret = LOS_MemPoolInit(pool, MEM_TC331_POOL_SIZE, 0);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    poolSize = LOS_MemPoolSizeGet(pool);
    firstNodeSize = poolSize - sizeof(TestMemPoolInfoBF) - OS_DLNK_HEAD_SIZE - TEST_BF_NODE_HEAD_SIZE;
    /* 请求尺寸使 allocSize 恰等于首节点尺寸 -> 不发生 split, 尾节点前邻为 used */
    requestSize = firstNodeSize - TEST_BF_NODE_HEAD_SIZE;

    ptr = LOS_MemAlloc(pool, requestSize);
    ICUNIT_GOTO_NOT_EQUAL(ptr, NULL, 0, EXIT);

    /* LOS_MemLastUsedGet 末邻 used 分支(bestfit 2013):
     * 返回值 = 节点地址 + 节点尺寸 + 节点头大小 = ptr + 请求尺寸 + 节点头偏移 */
    lastUsed = LOS_MemLastUsedGet(pool);
    ICUNIT_GOTO_EQUAL(lastUsed, (UINTPTR)ptr + requestSize + TEST_BF_NODE_HEAD_SIZE, lastUsed, EXIT);

    ret = LOS_MemFree(pool, ptr);
    if (ret == LOS_OK) {
        ptr = NULL;
    }
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

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

VOID ItLosMem331(VOID)
{
#ifdef LOSCFG_KERNEL_MEM_BESTFIT
    TEST_ADD_CASE("ItLosMem331", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

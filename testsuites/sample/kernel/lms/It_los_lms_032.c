/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "It_los_lms.h"

/* 用例简要描述: 仅用对外接口覆盖 LMS 检查池表管理边界 —— 空池注册返回 0;
 * 对已在检查表中的池重复调用 LOS_LmsAddCheckPool 命中"删旧节点再加新节点"
 * 重初始化分支; 对未注册地址/空指针调用 LOS_LmsDelCheckPool 命中"不在检查表"
 * 与空指针分支; 连续注册新池直至池表槽位耗尽(返回 0); 在池表满状态下
 * LOS_MemInit 新池(注册钩子返回 0, 池不进入检查表), 其后的小块(slab 路径)/
 * 大块(bestfit/tlsf 路径)/对齐分配与释放使各内存钩子(含对齐标记与释放前
 * 检查)命中"节点不存在"安全返回分支, 验证未注册池功能不受影响;
 * 最后清理恢复现场。 */

#define LMS032_EXTRA_POOL_SIZE 128
#define LMS032_EXTRA_POOL_CNT  60
/* TLSF 池头(位图+空闲链数组)较大, 堆缓冲需给数据区留足空间 */
#define LMS032_HEAP_SIZE       4096

static UINT8 g_lms032Pool[LMS032_EXTRA_POOL_SIZE];
static UINT8 g_lms032Extra[LMS032_EXTRA_POOL_CNT][LMS032_EXTRA_POOL_SIZE];
static UINT64 g_lms032Heap[LMS032_HEAP_SIZE / sizeof(UINT64)];

static UINT32 TestCase(VOID)
{
    UINT32 i;
    UINT32 ret;
    UINT32 realSize;
    UINT32 added = 0;
    UINT32 realSize2;
    VOID *ptr = NULL;
    VOID *ptr2 = NULL;
    VOID *ptrBig = NULL;

    /* 空池注册返回 0 */
    ret = LOS_LmsAddCheckPool(NULL, LMS032_EXTRA_POOL_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 正常注册 */
    realSize = LOS_LmsAddCheckPool(g_lms032Pool, LMS032_EXTRA_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(realSize, 0, realSize, EXIT);

    /* 重复注册同一池: 命中重初始化分支, 返回值一致 */
    realSize2 = LOS_LmsAddCheckPool(g_lms032Pool, LMS032_EXTRA_POOL_SIZE);
    ICUNIT_GOTO_EQUAL(realSize2, realSize, realSize2, EXIT);

    /* 删除未注册地址: 命中"不在检查表"分支 */
    LOS_LmsDelCheckPool(g_lms032Extra[0]);
    /* 空指针删除: 安全返回 */
    LOS_LmsDelCheckPool(NULL);

    /* 连续注册新池直至池表槽位耗尽(返回 0) */
    for (i = 0; i < LMS032_EXTRA_POOL_CNT; i++) {
        ret = LOS_LmsAddCheckPool(g_lms032Extra[i], LMS032_EXTRA_POOL_SIZE);
        if (ret == 0) {
            break;
        }
        added++;
    }
    ICUNIT_GOTO_NOT_EQUAL(added, 0, added, EXIT);

    /* 池表满: MemInit 的注册钩子返回 0, 新池不进入检查表, 按原始尺寸初始化 */
    (VOID)LOS_MemDeInit(g_lms032Heap);
    ret = LOS_MemInit(g_lms032Heap, LMS032_HEAP_SIZE);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 未注册池上的分配/释放: 各 LMS 钩子命中"节点不存在"安全返回分支 */
    ptr = LOS_MemAlloc(g_lms032Heap, 64); /* 64: 普通分配(slab 路径) */
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)ptr, (UINTPTR)NULL, (UINTPTR)ptr, EXIT);
    ptr2 = LOS_MemAlloc(g_lms032Heap, 64); /* 64: 普通分配(slab 路径) */
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)ptr2, (UINTPTR)NULL, (UINTPTR)ptr2, EXIT);
    (VOID)memset_s(ptr, 64, 0x5A, 64); /* 64: 写入分配区 */

    /* 大块分配绕过 slab, 走 bestfit/tlsf 路径: 释放触发检查钩子 */
    ptrBig = LOS_MemAlloc(g_lms032Heap, LMS032_HEAP_SIZE / 4); /* 大块走非 slab 路径 */
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)ptrBig, (UINTPTR)NULL, (UINTPTR)ptrBig, EXIT);
    ret = LOS_MemFree(g_lms032Heap, ptrBig);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* 对齐分配: 走对齐标记钩子(simpleMark)路径 */
    ptrBig = LOS_MemAllocAlign(g_lms032Heap, 64, 64); /* 64/64: 对齐分配 */
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)ptrBig, (UINTPTR)NULL, (UINTPTR)ptrBig, EXIT);
    ret = LOS_MemFree(g_lms032Heap, ptrBig);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemFree(g_lms032Heap, ptr2);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree(g_lms032Heap, ptr);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    (VOID)LOS_MemDeInit(g_lms032Heap);

EXIT:
    (VOID)LOS_MemDeInit(g_lms032Heap);
    LOS_LmsDelCheckPool(g_lms032Pool);
    for (i = 0; i < added; i++) {
        LOS_LmsDelCheckPool(g_lms032Extra[i]);
    }
    return LOS_OK;
}

VOID ItLosLms032(void)
{
    TEST_ADD_CASE("ItLosLms032", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}

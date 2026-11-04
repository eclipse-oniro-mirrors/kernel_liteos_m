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

/* 用例简要描述: 仅用对外接口(LOS_MemAlloc/Free + 自然访存)覆盖 LMS 错误报告
 * 矩阵 —— 数据区内投毒后自然写命中 REDZONE+STORE 报告; 释放后只读访问命中
 * AFTERFREE+LOAD 报告; 读内存池管理头(PAINT 区)命中"未知错误"+LOAD 报告;
 * LOS_LmsAddrProtect 投毒数据区后调用 LOS_MemFree, 释放前检查钩子触发
 * FREE 错误报告, 释放标记钩子再次触发 FREE 报告, 且释放流程正常完成
 * (noabort 语义, 堆一致性不受影响); 池头 dump 起点在池前的行全部跳过;
 * 最后全部释放并反初始化恢复现场。 */

#define LMS034_POOL_SIZE 2048
#define LMS034_ALLOC_SIZE 32

static UINT64 g_lms034Pool[LMS034_POOL_SIZE / sizeof(UINT64)];
static volatile UINT8 g_lms034Sink;

static UINT32 TestCase(VOID)
{
    UINT32 ret;
    UINT8 *memA = NULL;
    UINT8 *memB = NULL;
    UINT8 *memC = NULL;
    UINT8 *memD = NULL;

    (VOID)LOS_MemDeInit(g_lms034Pool);
    ret = LOS_MemInit(g_lms034Pool, LMS034_POOL_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    memA = (UINT8 *)LOS_MemAlloc(g_lms034Pool, LMS034_ALLOC_SIZE);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)memA, (UINTPTR)NULL, (UINTPTR)memA, EXIT);
    memB = (UINT8 *)LOS_MemAlloc(g_lms034Pool, LMS034_ALLOC_SIZE);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)memB, (UINTPTR)NULL, (UINTPTR)memB, EXIT);
    memC = (UINT8 *)LOS_MemAlloc(g_lms034Pool, LMS034_ALLOC_SIZE);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)memC, (UINTPTR)NULL, (UINTPTR)memC, EXIT);
    memD = (UINT8 *)LOS_MemAlloc(g_lms034Pool, LMS034_ALLOC_SIZE);
    ICUNIT_GOTO_NOT_EQUAL((UINTPTR)memD, (UINTPTR)NULL, (UINTPTR)memD, EXIT);

    /* 数据区内投毒后自然写: 命中 REDZONE+STORE 报告(不越界, 不破坏堆结构;
     * 越界写会破坏后继节点头/空闲链, 被节点完整性检查拒绝) */
    LOS_LmsAddrProtect((UINTPTR)memA + 8, (UINTPTR)memA + 16); /* 8/16: 数据区内投毒 */
    memA[12] = 0x55; /* 12: 投毒区内自然写 */
    LMS_FEED_WDT(); /* 每份报告后喂狗, 防止连续报告耗尽看门狗窗口 */
    LOS_LmsAddrDisableProtect((UINTPTR)memA + 8, (UINTPTR)memA + 16);

    /* 释放后读: 命中 AFTERFREE+LOAD 报告(只读不破坏空闲块;
     * 空闲块体内是分配器空闲链指针, 释放后写会破坏堆) */
    ret = LOS_MemFree(g_lms034Pool, memB);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    g_lms034Sink = memB[0];
    LMS_FEED_WDT();
    g_lms034Sink = memB[1];
    LMS_FEED_WDT();

    /* 读内存池管理头(PAINT 区): 命中"未知错误"影子值 + LOAD 报告,
     * 报告中的 dump 起点位于池首之前, 全部行跳过 */
    g_lms034Sink = *(UINT8 *)g_lms034Pool;
    LMS_FEED_WDT();

    /* 投毒后释放: 释放前检查与释放标记分别触发 FREE 错误报告,
     * 释放流程正常完成 */
    LOS_LmsAddrProtect((UINTPTR)memD, (UINTPTR)memD + 16); /* 16: 投毒数据区 */
    ret = LOS_MemFree(g_lms034Pool, memD);
    LMS_FEED_WDT();
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    ret = LOS_MemFree(g_lms034Pool, memA);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_MemFree(g_lms034Pool, memC);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

EXIT:
    (VOID)LOS_MemDeInit(g_lms034Pool);
    return LOS_OK;
}

VOID ItLosLms034(void)
{
    TEST_ADD_CASE("ItLosLms034", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}

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

/* 用例简要描述: 仅用对外接口覆盖多检查池边界报告与嵌套检查池 —— 同一静态
 * 缓冲上注册互不包含的检查池 A/B(中间留不属于任何池的间隙区): memset 起点
 * 位于 A 内已解除保护(可访问)的池尾、终点落入 B 的投毒区, 首字节检查放行而
 * 末字节检查命中, 报告路径中错误信息查询对"终点超出 A 池范围"的影子换算
 * 失败提前返回, 影子值保持无错误打印; 对 B 池投毒区做 8 字节宽自然读/写,
 * 命中 load8/store8 回调报告; 先注册内层子池再注册外层大池后对子池地址
 * 投毒, 池节点择优命中内层池(嵌套包含比较分支); 最后注销全部检查池恢复
 * 现场。注: 间隙区起点(报告地址不在任何检查池)的报告路径在当前内核实现
 * 中会对池节点空指针解引用(OsLmsGetErrorInfo 未防护), 属内核缺陷, 用例
 * 不构造该场景。 */

#define LMS037_POOL_SIZE   128
#define LMS037_BUF_SIZE    512
#define LMS037_POOL_B_OFF  144
#define LMS037_NESTED_SIZE 512
#define LMS037_SUB_OFF     128

static UINT64 g_lms037Buf[LMS037_BUF_SIZE / sizeof(UINT64)];
static UINT64 g_lms037Nested[LMS037_NESTED_SIZE / sizeof(UINT64)];
static volatile UINT64 g_lms037Sink;

static UINT32 TestCase(VOID)
{
    UINT32 retA;
    UINT32 retB;
    UINTPTR poolA = (UINTPTR)g_lms037Buf;
    UINTPTR poolB = (UINTPTR)g_lms037Buf + LMS037_POOL_B_OFF;

    /* 注册互不包含的 A/B 池(间隙区 [128,144) 不属于任何池) */
    retA = LOS_LmsAddCheckPool((VOID *)poolA, LMS037_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(retA, 0, retA, EXIT);
    retB = LOS_LmsAddCheckPool((VOID *)poolB, LMS037_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(retB, 0, retB, EXIT);

    /* A 池尾解除保护(可访问): memset 起点 A+8, 跨间隙落入 B 投毒区,
     * 末字节命中报告; 错误信息查询终点换算失败提前返回。
     * 注: 不构造"起点不在任何检查池"的报告场景 —— 当前内核 OsLmsGetErrorInfo
     * 对池外报告地址无空指针防护, 触发即解引用空节点(内核缺陷, 由内核侧修复)。 */
    LOS_LmsAddrDisableProtect(poolA, poolA + 16); /* 16: A 池头可访问区 */
    (VOID)memset((VOID *)(poolA + 8), 0x55, LMS037_POOL_B_OFF - 8 + 16);
    LMS_FEED_WDT(); /* 每份报告后喂狗 */

    /* 8 字节宽自然读/写 B 池投毒区: 命中 load8/store8 回调报告 */
    *(UINT64 *)(poolB + 16) = 0x1122334455667788; /* 16: 投毒区内写 */
    LMS_FEED_WDT();
    g_lms037Sink = *(UINT64 *)(poolB + 32); /* 32: 投毒区内读 */
    LMS_FEED_WDT();

    /* 嵌套检查池: 先注册内层子池, 再注册外层大池, 对子池地址投毒,
     * 池节点择优(包含比较)命中内层池 */
    retA = LOS_LmsAddCheckPool((VOID *)((UINTPTR)g_lms037Nested + LMS037_SUB_OFF), LMS037_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(retA, 0, retA, EXIT);
    retB = LOS_LmsAddCheckPool(g_lms037Nested, LMS037_NESTED_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(retB, 0, retB, EXIT);
    LOS_LmsAddrProtect((UINTPTR)g_lms037Nested + LMS037_SUB_OFF + 16,
        (UINTPTR)g_lms037Nested + LMS037_SUB_OFF + 32); /* 16/32: 子池内投毒 */
    LOS_LmsAddrDisableProtect((UINTPTR)g_lms037Nested + LMS037_SUB_OFF + 16,
        (UINTPTR)g_lms037Nested + LMS037_SUB_OFF + 32);
    LOS_LmsDelCheckPool(g_lms037Nested);
    LOS_LmsDelCheckPool((VOID *)((UINTPTR)g_lms037Nested + LMS037_SUB_OFF));

EXIT:
    LOS_LmsDelCheckPool(g_lms037Nested);
    LOS_LmsDelCheckPool((VOID *)((UINTPTR)g_lms037Nested + LMS037_SUB_OFF));
    LOS_LmsDelCheckPool((VOID *)poolB);
    LOS_LmsDelCheckPool((VOID *)poolA);
    return LOS_OK;
}

VOID ItLosLms037(void)
{
    TEST_ADD_CASE("ItLosLms037", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}

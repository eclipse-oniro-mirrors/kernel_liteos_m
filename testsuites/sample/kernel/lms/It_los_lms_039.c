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
 * 3. Neither the name of the copyright holder nor of its contributors may be used
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

/* 用例简要描述: 【BUG_09 缺陷复现用例(默认隔离)】仅用对外接口构造"错误报告
 * 地址不在任何检查池"的边界场景 —— 注册互不包含的检查池 A/B(中间留不属于
 * 任何检查池的间隙区), memset 起点位于间隙区、终点落入 B 投毒区, 拦截器
 * storeN 首字节检查对池外地址放行、末字节检查命中, 触发 STORE 错误报告。
 *
 * 【现象】    LMS 错误报告路径对"报告地址不在任何检查池"的场景空指针解引用:
 *            qemu/MPS2 低地址为可读 flash, 以垃圾值继续输出错误信息(错误
 *            地址/影子地址/影子值/错误分类均为垃圾, 取证日志见问题 #50);
 *            低地址不可映射单板(如 ws63/RISC-V)预期数据访问异常崩溃/挂死。
 * 【根因】    los_lms.c OsLmsGetErrorInfo(:579): OsLmsGetPoolNodeFromAddr 对
 *            池外地址返回 NULL, 未判空即传入 OsLmsGetShadowInfo, 其内部
 *            OsLmsMem2Shadow(:233) 解引用 node->poolAddr(读取近零地址)。
 * 【修复方向】OsLmsGetErrorInfo 增加 node==NULL 判空提前返回(保持 info 零值,
 *            错误信息按"无错误"打印, 内存打印以地址 0 命中"不在检查池"提示)。
 * 【恢复方法】修复后把 bug_isolate.h 中 KERNEL_BUG_09_LMS_ERRINFO_NULL 置 1,
 *            聚合注册点(It_los_lms.c)恢复调用, 用例恢复执行,
 *            作为回归看护用例常跑。
 * 【关联】    BUG_09 / question_record 问题 #50(完整取证日志与根因分析见
 *            question_artifacts/20260922_171449/)。
 */

#include "osTest.h"
#include "It_los_lms.h"

#define LMS039_POOL_SIZE  128
#define LMS039_BUF_SIZE   512
#define LMS039_POOL_B_OFF 144

static UINT64 g_lms039Buf[LMS039_BUF_SIZE / sizeof(UINT64)];

static UINT32 TestCase(VOID)
{
    UINT32 retA;
    UINT32 retB;
    UINTPTR poolA = (UINTPTR)g_lms039Buf;
    UINTPTR poolB = (UINTPTR)g_lms039Buf + LMS039_POOL_B_OFF;

    /* 注册互不包含的 A/B 检查池(间隙区不属于任何检查池) */
    retA = LOS_LmsAddCheckPool((VOID *)poolA, LMS039_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(retA, 0, retA, EXIT);
    retB = LOS_LmsAddCheckPool((VOID *)poolB, LMS039_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(retB, 0, retB, EXIT);

    /* 缺陷触发: memset 起点位于间隙区(报告地址不在任何检查池),
     * 终点落入 B 投毒区 → STORE 报告 → OsLmsGetErrorInfo 空节点解引用 */
    (VOID)memset((VOID *)(poolA + LMS039_POOL_SIZE + 8), 0x55,
        LMS039_POOL_B_OFF - LMS039_POOL_SIZE - 8 + 16);

EXIT:
    LOS_LmsDelCheckPool((VOID *)poolB);
    LOS_LmsDelCheckPool((VOID *)poolA);
    return LOS_OK;
}

VOID ItLosLms039(void)
{
    TEST_ADD_CASE("ItLosLms039", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}

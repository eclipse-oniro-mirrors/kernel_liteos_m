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

/* 用例简要描述: 【BUG_10 缺陷回归看护用例(默认隔离)】仅用对外接口看护
 * LOS_LmsAddCheckPool 的"小池 realSize 归零不得占用池表槽位"契约 —— 注册
 * 64 字节小池(64/17*16=48, align64 后 realSize=0, 返回 0 视为未注册),
 * 填表至满并确认, 随后删除小池并重试注册: 小池未注册则不应释放任何槽位,
 * 重试必须仍失败(表保持满)。
 *
 * 【现象】    当前(未修复)内核: 小池注册返回 0 的同时将 poolSize=0 死节点
 *            注册进检查表占用槽位 —— 删除小池后槽位立即恢复, 重试注册成功,
 *            本用例末段断言失败(红灯)。错误码歧义: 返回 0 与空池/池表满
 *            同码无法区分(问题 #51)。
 * 【根因】    los_lms.c LOS_LmsAddCheckPool: :154 先 OsLmsCheckPoolCreate
 *            占槽, :160 后算 realSize, 小池(<68B)归零不回滚仍以 poolSize=0
 *            注册(:162-170), :173 返回 0; 内存管理器按 resize==0 回退全尺寸,
 *            与表内死节点状态不一致。
 * 【修复方向】realSize==0 时不注册并回滚槽位(返回 0 维持"未注册"语义);
 *            若改为可区分错误码, 需同步调整 bestfit/tlsf/bestfit_little 三处
 *            resize==0 判断及本用例首段断言。
 * 【恢复方法】修复后把 bug_isolate.h 中 KERNEL_BUG_10_LMS_ADDPOOL_DEADNODE
 *            置 1, 聚合注册点(It_los_lms.c)恢复调用, 用例恢复执行,
 *            作为回归看护用例常跑。
 * 【关联】    BUG_10 / question_record 问题 #51(完整分析与双板日志见
 *            question_artifacts/20260922_213645/)。
 */

#include "osTest.h"
#include "It_los_lms.h"

#define LMS040_SMALL_POOL_SIZE 64   /* 64/17*16=48, align64 后 realSize=0 */
#define LMS040_EXTRA_POOL_SIZE 128  /* 128/17*16=112, align64 后 realSize=64 */
#define LMS040_EXTRA_POOL_CNT  60   /* 覆盖 qemu(10)/ws63(50) 池表上限 */

static UINT8 g_lms040Small[LMS040_SMALL_POOL_SIZE];
static UINT8 g_lms040Extra[LMS040_EXTRA_POOL_CNT][LMS040_EXTRA_POOL_SIZE];
static volatile UINT8 g_lms040Sink;

static UINT32 TestCase(VOID)
{
    UINT32 i;
    UINT32 ret;
    UINT32 filled = 0;

    /* 小池注册: realSize 归零返回 0, 语义为"未注册"(不得占用槽位) */
    ret = LOS_LmsAddCheckPool(g_lms040Small, LMS040_SMALL_POOL_SIZE);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* 未注册小池地址的自然访问不触发检查(不崩溃) */
    g_lms040Sink = g_lms040Small[0];

    /* 填表至满 */
    for (i = 0; i < LMS040_EXTRA_POOL_CNT; i++) {
        ret = LOS_LmsAddCheckPool(g_lms040Extra[i], LMS040_EXTRA_POOL_SIZE);
        if (ret == 0) {
            break;
        }
        filled++;
    }
    ICUNIT_GOTO_NOT_EQUAL(filled, 0, filled, EXIT);
    ICUNIT_GOTO_EQUAL(i, filled, i, EXIT); /* 循环因表满中断而非计数耗尽 */

    /* 表满确认: 同一缓冲重试失败 */
    ret = LOS_LmsAddCheckPool(g_lms040Extra[filled], LMS040_EXTRA_POOL_SIZE);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

    /* 核心契约: 小池未注册, 删除它不得释放任何槽位, 重试必须仍失败。
     * (未修复内核: 死节点被删除释放槽位, 重试成功, 本断言失败) */
    LOS_LmsDelCheckPool(g_lms040Small);
    ret = LOS_LmsAddCheckPool(g_lms040Extra[filled], LMS040_EXTRA_POOL_SIZE);
    ICUNIT_GOTO_EQUAL(ret, 0, ret, EXIT);

EXIT:
    LOS_LmsDelCheckPool(g_lms040Extra[filled]);
    for (i = 0; i < filled; i++) {
        LOS_LmsDelCheckPool(g_lms040Extra[i]);
    }
    LOS_LmsDelCheckPool(g_lms040Small);
    return LOS_OK;
}

VOID ItLosLms040(void)
{
    TEST_ADD_CASE("ItLosLms040", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}

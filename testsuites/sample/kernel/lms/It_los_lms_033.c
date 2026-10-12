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

/* 用例简要描述: 仅用对外接口覆盖 LOS_LmsAddrProtect/DisableProtect 的影子标记
 * 矩阵 —— 保护区间落在同一影子字节(16 字节窗口)内与跨影子字节(含首/尾字节
 * 部分掩码与整段中段填充)的各种组合; 保护区间终点越过检查池范围时, 影子地址
 * 换算失败打印错误并使影子设置提前返回; 终点小于等于起点、起点不在任何检查池
 * 内时安全忽略; 对已投毒区域做自然读访问, 触发 LOAD 错误报告验证保护生效;
 * 最后解除保护并注销检查池恢复现场。 */

#define LMS033_POOL_SIZE 1024

static UINT64 g_lms033Pool[LMS033_POOL_SIZE / sizeof(UINT64)];
static UINT8 g_lms033Outside[64]; /* 64: 不注册到检查表的对照缓冲 */
static volatile UINT8 g_lms033Sink;

static UINT32 TestCase(VOID)
{
    UINT32 realSize;
    UINTPTR base = (UINTPTR)g_lms033Pool;
    UINTPTR poolEnd;

    realSize = LOS_LmsAddCheckPool(g_lms033Pool, LMS033_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(realSize, 0, realSize, EXIT);
    poolEnd = base + realSize;

    /* 终点小于等于起点: 安全忽略 */
    LOS_LmsAddrProtect(base + 32, base + 16); /* 32/16: 逆序区间 */
    LOS_LmsAddrDisableProtect(base + 32, base + 16);

    /* 起点不在任何检查池: 安全忽略 */
    LOS_LmsAddrProtect((UINTPTR)g_lms033Outside, (UINTPTR)g_lms033Outside + 32); /* 32: 池外区间 */
    LOS_LmsAddrDisableProtect((UINTPTR)g_lms033Outside, (UINTPTR)g_lms033Outside + 32);

    /* 同一影子字节内的区间(16 字节窗口): 首尾偏移掩码分支 */
    LOS_LmsAddrProtect(base + 64, base + 68); /* 64/68: 同窗口 */
    LOS_LmsAddrDisableProtect(base + 64, base + 68);

    /* 跨影子字节: 首字节部分掩码 + 整段中段填充 */
    LOS_LmsAddrProtect(base + 68, base + 92); /* 68/92: 首部偏移 */
    LOS_LmsAddrDisableProtect(base + 68, base + 92);

    /* 跨影子字节: 尾字节部分掩码 + 整段中段填充 */
    LOS_LmsAddrProtect(base + 80, base + 100); /* 80/100: 尾部偏移 */
    LOS_LmsAddrDisableProtect(base + 80, base + 100);

    /* 跨影子字节: 首尾均部分掩码 + 中段填充 */
    LOS_LmsAddrProtect(base + 68, base + 100); /* 68/100: 首尾偏移 */
    LOS_LmsAddrDisableProtect(base + 68, base + 100);

    /* 跨影子字节: 首尾均对齐到影子字节边界, 仅中段填充 */
    LOS_LmsAddrProtect(base + 64, base + 80); /* 64/80: 窗口对齐 */
    LOS_LmsAddrDisableProtect(base + 64, base + 80);

    /* 保护区间终点越过检查池范围: 影子换算失败打印错误并提前返回 */
    LOS_LmsAddrProtect(base + 64, poolEnd + 256); /* 256: 越过池尾 */
    LOS_LmsAddrDisableProtect(base + 64, poolEnd + 256);

    /* 投毒后自然读访问: 触发 LOAD 错误报告(REDZONE), 验证保护生效 */
    LOS_LmsAddrProtect(base + 128, base + 192); /* 128/192: 投毒区间 */
    g_lms033Sink = *(UINT8 *)(base + 160); /* 160: 投毒区内自然读 */
    LOS_LmsAddrDisableProtect(base + 128, base + 192);

    /* 解除后再访问不再报告(不崩溃) */
    g_lms033Sink = *(UINT8 *)(base + 160);

EXIT:
    LOS_LmsDelCheckPool(g_lms033Pool);
    return LOS_OK;
}

VOID ItLosLms033(void)
{
    TEST_ADD_CASE("ItLosLms033", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}

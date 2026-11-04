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

/* 用例简要描述: 仅用对外接口覆盖检查池尾部边界的错误报告 dump 路径 ——
 * 对小尺寸检查池尾部区域投毒后自然读触发 LOAD 错误报告, 报告中的内存
 * 打印自投毒地址向前回溯, 当打印行越过检查池上界时提前结束(池尾 dump
 * 截断分支); 解除保护区间终点越过池范围时影子换算失败打印并提前返回;
 * 注销检查池后再访问原池区域不再报告(不崩溃), 验证注销生效。 */

#define LMS038_POOL_SIZE 256
#define LMS038_PROT_OFF  160
#define LMS038_PROT_LEN  16
#define LMS038_READ_OFF  168

static UINT64 g_lms038Pool[LMS038_POOL_SIZE / sizeof(UINT64)];
static volatile UINT8 g_lms038Sink;

static UINT32 TestCase(VOID)
{
    UINT32 realSize;

    realSize = LOS_LmsAddCheckPool(g_lms038Pool, LMS038_POOL_SIZE);
    ICUNIT_GOTO_NOT_EQUAL(realSize, 0, realSize, EXIT);

    /* 池尾投毒: LOAD 报告的内存打印行越过检查池上界时提前结束 */
    LOS_LmsAddrProtect((UINTPTR)g_lms038Pool + LMS038_PROT_OFF,
        (UINTPTR)g_lms038Pool + LMS038_PROT_OFF + LMS038_PROT_LEN);
    g_lms038Sink = *(UINT8 *)((UINTPTR)g_lms038Pool + LMS038_READ_OFF);

    /* 解除保护: 终点越过池范围, 影子换算失败打印并提前返回 */
    LOS_LmsAddrDisableProtect((UINTPTR)g_lms038Pool,
        (UINTPTR)g_lms038Pool + LMS038_POOL_SIZE * 2); /* 2 倍池宽越界 */

    /* 正常解除池尾保护 */
    LOS_LmsAddrDisableProtect((UINTPTR)g_lms038Pool + LMS038_PROT_OFF,
        (UINTPTR)g_lms038Pool + LMS038_PROT_OFF + LMS038_PROT_LEN);

    /* 注销检查池后再访问原区域: 不再报告且不崩溃 */
    LOS_LmsDelCheckPool(g_lms038Pool);
    g_lms038Sink = *(UINT8 *)((UINTPTR)g_lms038Pool + LMS038_READ_OFF);
    g_lms038Sink = *(UINT8 *)g_lms038Pool;

    return LOS_OK;

EXIT:
    LOS_LmsDelCheckPool(g_lms038Pool);
    return LOS_OK;
}

VOID ItLosLms038(void)
{
    TEST_ADD_CASE("ItLosLms038", TestCase, TEST_LOS, TEST_LMS, TEST_LEVEL1, TEST_FUNCTION);
}

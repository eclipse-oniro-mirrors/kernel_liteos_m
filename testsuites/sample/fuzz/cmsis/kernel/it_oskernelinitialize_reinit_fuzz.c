/*
 * Copyright (c) 2022-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to
 *    endorse or promote products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ItFuzzOsKernelReinit: osKernelInitialize 于内核 Running 态重入
 *
 * 【现象】内核 Running 态下调用 osKernelInitialize 触发 LMS 非法写（arm）。
 * 【根因】g_kernelState 状态 guard 失效，Running 态重入仍会再次执行 LOS_KernelInit
 * —— 重新初始化内存池/调度链表等使用中的全局结构。
 * 【修复方向】修正 g_kernelState guard，非 Inactive 态返回 osError；修复后与 BUG_01
 * 一并恢复。
 * 【恢复方法】修复后把 bug_isolate.h 中 FUZZ_BUG_S11_OSKERNEL_REINIT 置 1，
 * 本用例恢复编译执行，作为回归看护用例常跑。
 * 【关联】同源: 本文件 BUG_01(ItLosTestCmsisKernel008)
 */

#include "it_cmsis_kernel_fuzz.h"

static void __attribute__((unused)) FzCmsisThreadEntry(void *arg) { (VOID)arg; osDelay(1); }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();
    for (int i = 0; i < 1; i++) {
        FuzzWatchdogFeed();
        (VOID)osKernelInitialize();
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}


VOID ItFuzzOsKernelReinit(VOID)
{
    TEST_ADD_CASE("ItFuzzOsKernelReinit", TestCase, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
}

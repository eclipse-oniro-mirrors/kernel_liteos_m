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
 * ItFuzzPthreadCleanupTaskCtx: pthread_cleanup_push/pop 于非 pthread 任务上下文调用
 *
 * 【现象】LOS 任务（非 pthread）上下文调用 pthread_cleanup_push 触发 Load
 * access fault（ws63：mepc=pthread_cleanup_push_inner+0x14, mtval=0x28）。
 * 【根因】lib/posix/src/pthread.c:75 __pthread_self() 直接强转
 * OsCurrTaskGet()->arg 为 PthreadData* 无校验，cleanup_push_inner 解引用
 * self->cancelbuf（偏移 0x28）——非 pthread 任务 arg 为 NULL → NULL+0x28
 * 野读；同文件其余 API 均有 IsPthread 防护唯此遗漏（问题 39）。
 * 【修复方向】cleanup_push/pop_inner 入口加 IsPthread 检查。
 * 【恢复方法】修复后把 bug_isolate.h 中 FUZZ_BUG_S13_CLEANUP_NONTASK 置 1，
 * 本探针恢复编译执行，验证主任务上下文调用不再崩溃。
 */
#include "it_posix_pthread_fuzz.h"

static VOID FzCleanupProbe(VOID *arg) { (VOID)arg; }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();
    for (int i = 0; i < 1; i++) {
        FuzzWatchdogFeed();
        /* non-pthread task context: pre-fix this dereferences NULL+0x28 */
        pthread_cleanup_push(FzCleanupProbe, NULL);
        pthread_cleanup_pop(0);
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}


VOID ItFuzzPthreadCleanupTaskCtx(VOID)
{
    TEST_ADD_CASE("ItFuzzPthreadCleanupTaskCtx", TestCase, TEST_LIB, TEST_PTHREAD, TEST_LEVEL0, TEST_FUNCTION);
}

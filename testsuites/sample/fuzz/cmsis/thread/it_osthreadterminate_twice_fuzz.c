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
 * ItFuzzOsThreadTerminateTwice: osThreadTerminate 已终止线程
 *
 * 【现象】对已终止线程再次 osThreadTerminate 触发野 id 解引用（arm）。
 * 【根因】thread_id 即 LosTaskCB 指针；任务终止后 TCB 已回收（可能已被新任务复用），
 * osThreadTerminate 直接 pstTaskCB->taskId 取 taskId 去 LOS_TaskDelete，
 * 无任务状态校验 —— 野读 taskId，甚至可能误删复用该 TCB 的新任务。
 * 【修复方向】适配层校验 TCB 处于非 UNUSED/已终止状态后再取 taskId。
 * 【恢复方法】修复后把 bug_isolate.h 中 FUZZ_BUG_S10_OSTHR_DBLTERM 置 1，
 * 本用例恢复编译执行，作为回归看护用例常跑。
 */

#include "it_cmsis_thread_fuzz.h"

static void __attribute__((unused)) FzCmsisThreadEntry(void *arg) { (VOID)arg; osDelay(1); }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();
    osThreadId_t tid;
    for (int i = 0; i < 1; i++) {
        FuzzWatchdogFeed();
        tid = osThreadNew(FzCmsisThreadEntry, NULL, NULL);
        if (tid != NULL) {
            (VOID)osThreadTerminate(tid);
            LOS_TaskDelay(1);
            (VOID)osThreadTerminate(tid);
        }
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}


VOID ItFuzzOsThreadTerminateTwice(VOID)
{
    TEST_ADD_CASE("ItFuzzOsThreadTerminateTwice", TestCase, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
}

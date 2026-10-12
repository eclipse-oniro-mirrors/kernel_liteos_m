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
 * ItFuzzOsSemAcquireWildId: osSemaphoreAcquire 传入野 id
 *
 * 【现象】传入野 id（如 0x10001）调用 osSemaphoreAcquire((id, 3)）触发 load access fault
 * （arm exc 0x10010004），未按 CMSIS-RTOS2 语义返回 osErrorParameter。
 * 【根因】compat/cmsis/cmsis_liteos2.c 的 osSemaphoreAcquire() 仅校验 id==NULL 与 ISR
 * 上下文，随即 ((LosSemCB *)id)->semID 解引用取内部句柄 —— 未校验 id 是否
 * 为本池已分配的信号量 CB（semStat==OS_SEM_USED）。
 * 【修复方向】适配层对 id 指向的 LosSemCB 做已分配状态校验后再取 semID。
 * 【恢复方法】修复后把 bug_isolate.h 中 FUZZ_BUG_S01_CMSIS_SEMWILD 置 1，
 * 本用例恢复编译执行，作为回归看护用例常跑。
 */

#include "it_cmsis_sem_fuzz.h"

static void __attribute__((unused)) FzCmsisThreadEntry(void *arg) { (VOID)arg; osDelay(1); }

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();
    for (int i = 0; i < 1; i++) {
        FuzzWatchdogFeed();
        (VOID)osSemaphoreAcquire((osSemaphoreId_t)(UINTPTR)0x10001, 3);
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}


VOID ItFuzzOsSemAcquireWildId(VOID)
{
    TEST_ADD_CASE("ItFuzzOsSemAcquireWildId", TestCase, TEST_COMPAT, TEST_CMSIS, TEST_LEVEL0, TEST_FUNCTION);
}

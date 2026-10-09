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
 * ItFuzzHwiDeleteSystemIrq: 删除系统在用中断（ws63 TIMER_1，tick 依赖）
 *
 * 【现象】LOS_HwiDelete(TIMER_1_IRQN=28) 静默删掉系统 tick 中断 → tick 停
 * → 喂狗停 → 看门狗复位（问题 40：fuzz 中断号变异曾致 6/7 全量复位）。
 * 【根因】kernel/base/los_hwi.c LOS_HwiDelete 仅校验号段，无"系统在用
 * 中断"防护（对照 LOS_TaskDelete 的 OS_TASK_FLAG_SYSTEM 防护）。
 * 【修复方向】系统中断注册/占用校验，删除在用系统中断返回错误码。
 * 【恢复方法】修复后把 bug_isolate.h 中 FUZZ_BUG_S14_HWI_DEL_SYSTEM 置 1，
 * 本探针恢复编译执行，期待删除被拒绝（非 LOS_OK）且系统继续运行。
 */
#include "it_los_misc_fuzz.h"

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();
    for (int i = 0; i < 1; i++) {
        FuzzWatchdogFeed();
        /* ws63 TIMER_1_IRQN = 28 (chip_core_irq.h), the tick source;
         * pre-fix: silently deleted -> watchdog reset; post-fix: rejected */
        (VOID)LOS_HwiDelete(28, NULL);
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}


VOID ItFuzzHwiDeleteSystemIrq(VOID)
{
    TEST_ADD_CASE("ItFuzzHwiDeleteSystemIrq", TestCase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
}

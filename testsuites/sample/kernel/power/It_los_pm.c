/*
 * Copyright (c) 2021-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
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
#include "It_los_pm.h"

VOID ItSuiteLosPm(VOID)
{
    ItLosPm001();
    ItLosPm002();
    ItLosPm003();
    ItLosPm004();
#if (LOSCFG_KERNEL_TICK_PERIODIC == 1)
    ItLosPm005(); /* PM 深覆盖: 周期 tick 板无 BUG_11 竞态路径, 照常执行 */
#else
    /* tickless 板(qemu): ItLosPm005 触发 BUG_11 断言竞态, 修复前隔离 */
#if defined(KERNEL_BUG_11_PM_TICKTIMER_ASSERT) && KERNEL_BUG_11_PM_TICKTIMER_ASSERT == 1
    ItLosPm005(); /* PM 深覆盖: sysctrl 全字段/prepare 失败分支/三档模式/tickless 定时器链 */
#endif
#endif
    ItLosPm006();
    /* BUG_21(resleak 批次): PM 唤醒锁全局计数打满后新锁节点插入不回滚泄漏,
     * 修复前隔离(红灯暴露用例, 断言修复后契约) */
#if defined(KERNEL_BUG_21_PM_LOCKCNT_FULL_LEAK) && KERNEL_BUG_21_PM_LOCKCNT_FULL_LEAK == 1
    ItLosPm007();
#endif
}


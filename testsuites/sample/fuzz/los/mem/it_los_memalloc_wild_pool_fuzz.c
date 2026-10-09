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
 * ItFuzzLosMemAllocWildPool: LOS_MemAlloc 传入野池地址
 *
 * 【现象】LOS_MemAlloc 传入野池地址（如 0x20000000）触发 load access fault（ws63）。
 * 【根因】LOS_MemAlloc 仅校验 pool==NULL/size==0，随即把传入地址当作
 * LosMemPoolInfo 池头加锁并行走空闲链表 —— 无池头魔数/注册范围校验。
 * 【修复方向】进入分配路径前校验池头合法性（魔数/已注册范围）。
 * 【恢复方法】修复后把 bug_isolate.h 中 FUZZ_BUG_S05_LOSMEM_WILDPOOL 置 1，
 * 本用例恢复编译执行，作为回归看护用例常跑。
 */

#include "it_los_mem_fuzz.h"

static VOID __attribute__((unused)) FzTaskEntry(UINT32 arg)
{
    for (;;) { LOS_TaskDelay(10 + (arg & 0xF)); }
}

static UINT32 TestCase(VOID)
{
    INIT_FuzzEnvironment();
    for (int i = 0; i < 1; i++) {
        FuzzWatchdogFeed();
        (VOID)LOS_MemAlloc((VOID *)(UINTPTR)0x20000000U, 32);
    }
    DT_Clear(g_element);
    CLEAR_FuzzEnvironment();
    return 0;
}


VOID ItFuzzLosMemAllocWildPool(VOID)
{
    TEST_ADD_CASE("ItFuzzLosMemAllocWildPool", TestCase, TEST_LOS, TEST_MEM, TEST_LEVEL0, TEST_FUNCTION);
}

/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the following conditions:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "it_los_hwi.h"

#define HWI_UNLOCK_SLEEP_TICKS 10
#define HWI_UNLOCK_MIN_ADV_TICK 8

/* 用例简要描述: 中断解锁主流程:返回值三架构分叉契约 + 解锁后 tick 恢复投递行为验证 */
static UINT32 Testcase(VOID)
{
    UINT32 lockRet;
    UINT32 unLockRet;
    UINT64 tickBefore;
    UINT64 tickAfter;

    /* 用例运行于任务上下文, 前置: 中断处于使能态 */
    /* 锁定: LOS_IntLock(->ArchIntLock) 返回锁前状态并关中断 */
    lockRet = LOS_IntLock();
    /* 解锁: LOS_IntUnLock(->ArchIntUnLock) 返回解锁前锁定态并重新开中断 */
    unLockRet = LOS_IntUnLock();

    /* 返回值位语义三架构分叉 (各架构 los_dispatch.S 的 ArchIntLock/ArchIntUnLock,
     * 双板返回值形态不同已登记坑表, 断言按各自位语义显式接受: ItLosHwi055 余量法同批先例) */
#if defined(LOSCFG_ARCH_RISCV)
    /* riscv(los_dispatch.S:119): csrr a0, mstatus 后置 MIE -> 返回完整旧 mstatus,
     * MIE=bit3: 锁前=1(使能), 解锁前=0(锁定) */
    ICUNIT_ASSERT_EQUAL(((lockRet >> 3) & 1), 1, lockRet);
    ICUNIT_ASSERT_EQUAL(((unLockRet >> 3) & 1), 0, unLockRet);
#elif defined(LOSCFG_ARCH_ARM_AARCH64)
    /* arm64(los_dispatch.S:69): mrs x0, daif 后清掩蔽 -> 返回完整旧 DAIF,
     * I 位=bit7/0x80(与 ArchIntLocked 同掩码口径): 锁前=0(使能), 解锁前=1(锁定) */
    ICUNIT_ASSERT_EQUAL((lockRet & 0x80), 0, lockRet);
    ICUNIT_ASSERT_EQUAL((unLockRet & 0x80), 0x80, unLockRet);
#else
    /* arm cortex_m(los_dispatch.S:171): mrs r0, primask 后 cpsie i -> 返回旧
     * PRIMASK 全值: 锁前=0(未掩蔽), 解锁前=1(已掩蔽) */
    ICUNIT_ASSERT_EQUAL(lockRet, 0, lockRet);
    ICUNIT_ASSERT_EQUAL(unLockRet, 1, unLockRet);
#endif

    /* 行为验证(余量法): 解锁后中断恢复投递, 睡 HWI_UNLOCK_SLEEP_TICKS 断言 tick
     * 增量 >= HWI_UNLOCK_MIN_ADV_TICK(容忍 tick 相位缺失 1~2, 若解锁未生效则
     * TaskDelay 永不醒来直接失败; ItLosHwi055 余量法先例) */
    tickBefore = LOS_TickCountGet();
    LOS_TaskDelay(HWI_UNLOCK_SLEEP_TICKS);
    tickAfter = LOS_TickCountGet();
    ICUNIT_ASSERT_EQUAL(((tickAfter - tickBefore) >= (UINT64)HWI_UNLOCK_MIN_ADV_TICK), TRUE,
                        (UINT32)(tickAfter - tickBefore));

    return LOS_OK;
}

/**
 * @ingroup TEST_FN_KERNEL_IRQ
 * @par TestCase_Number
 * ItLosHwi058
 * @par TestCase_TestCase_Type
 * Function test
 * @brief LOS_IntUnLock return-state contract (per-arch) and irq-redelivery behaviour
 * @par TestCase_Pretreatment_Condition
 * task context, interrupts enabled.
 * @par TestCase_Test_Steps
 * step1: LOS_IntLock then LOS_IntUnLock, capture both return values.
 * step2: assert per-arch bit semantics of both returns (cortex-m PRIMASK / riscv mstatus.MIE / arm64 DAIF.I).
 * step3: LOS_TaskDelay(10) and assert tick advanced by at least 8 (irqs really re-enabled).
 * @par TestCase_Expected_Result
 * 1.Unlock return encodes the locked state before unlocking, on every arch bit-field convention.
 * 2.Tick count advances after unlock, proving interrupt redelivery.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * LOS_IntUnLock is a macro alias of ArchIntUnLock (los_interrupt.h:701).
 */

VOID ItLosHwi058(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHwi058", Testcase, TEST_LOS, TEST_FN_KERNEL_IRQ, TEST_LEVEL0, TEST_FUNCTION);
}

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

#include "it_los_misc.h"
#include "los_hw.h"

/* LOS_CpuInfo(los_hw.h:67 STATIC INLINE)转调 ArchCpuInfo, 实现仅存在于
 * arch/arm64/los_cpu.c(aarch64: midr_el1 partNo 查 g_cpuTable, 未命中返回
 * "unknown")与 arch/arm/cortex_a_r/cpu.c(armv7-a); cortex-m/riscv 板无实现
 * (调用即链接失败) -> 整文件架构门控, 非 A 板按 R10 登记(不能覆盖) */
#if defined(LOSCFG_ARCH_ARM_AARCH64) || defined(LOSCFG_ARCH_ARM_V7A)

/* 用例简要描述: 多核信息查询接口:ArchCpuInfo midr 查表返回 CPU 名契约(非空非空串) */
static UINT32 TestCase(VOID)
{
    const CHAR *cpuName;

    cpuName = LOS_CpuInfo();
    /* midr partNo 查表命中返回型号名(如 rk3568 从核 Cortex-A55 -> "Cortex-A55"),
     * 未命中返回 "unknown"; 契约恒为非 NULL 且非空串(不断言具体型号, 不绑板) */
    ICUNIT_ASSERT_NOT_EQUAL((UINTPTR)cpuName, (UINTPTR)NULL, (UINT32)(UINTPTR)cpuName);
    ICUNIT_ASSERT_EQUAL((cpuName[0] != '\0'), TRUE, cpuName[0]);

    return LOS_OK;
}

#endif /* LOSCFG_ARCH_ARM_AARCH64 || LOSCFG_ARCH_ARM_V7A */

/**
 * @ingroup TEST_FN_KERNEL_ARCH
 * @par TestCase_Number
 * ItLosHw001
 * @par TestCase_TestCase_Type
 * Function test
 * @brief LOS_CpuInfo returns a non-empty CPU model name via ArchCpuInfo midr lookup
 * @par TestCase_Pretreatment_Condition
 * aarch64 or armv7-a board (ArchCpuInfo implemented; whole file arch-gated).
 * @par TestCase_Test_Steps
 * step1: call LOS_CpuInfo, capture returned string.
 * @par TestCase_Expected_Result
 * 1.Return value is non-NULL and non-empty (model name on table hit, "unknown" on miss).
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * Runs on rk3568_m/rk3588_m (AARCH64) and hi3516 (V7A) domains; R10-registered elsewhere.
 */

VOID ItLosHw001(VOID) // IT_Layer_ModuleORFeature_No
{
#if defined(LOSCFG_ARCH_ARM_AARCH64) || defined(LOSCFG_ARCH_ARM_V7A)
    TEST_ADD_CASE("ItLosHw001", TestCase, TEST_LOS, TEST_FN_KERNEL_ARCH, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

/*
 * Copyright (c) 2022-2022 Huawei Device Co., Ltd. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "it_los_hwi.h"

static VOID HwiF01(VOID)
{
    TestHwiClear(HWI_NUM_TEST);
    g_testCount++;
}

#ifndef LOSCFG_PLATFORM_WS63_M
/* qemu 注入桩：使 setIrqPriority 固定失败，驱动 LOS_HwiCreate 走失败回退分支 */
static UINT32 HwiSetPriorityFail(HWI_HANDLE_T hwiNum, HWI_PRIOR_T priority)
{
    (VOID)hwiNum;
    (VOID)priority;
    return LOS_NOK;
}
#endif

/* 用例简要描述: LOS_HwiCreate（对外接口）优先级设置失败回退路径（los_hwi.c 148-153）——
 * ws63 传越界优先级（>=7）真实触发 setIrqPriority 失败；qemu 通过对外接口
 * LOS_HwiOpsGet 临时替换 setIrqPriority 为失败桩；均验证失败后 OsHwiDel
 * 回退清理表项（同号再创建成功，无 ALREADY_CREATED 残留），最后删除恢复 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    HWI_MODE_T mode = 0;

    g_testCount = 0;
    /* 前置防御：清理 HWI_NUM_TEST 可能的历史残留（ws63 顺带停 timer1） */
    (VOID)TestHwiDelete(HWI_NUM_TEST);

#ifdef LOSCFG_PLATFORM_WS63_M
    /* F-Inv: 优先级 7 越界 → setIrqPriority 失败 → OsHwiDel 回退清理表项 */
    ret = LOS_HwiCreate(HWI_NUM_TEST, 7, mode, (HWI_PROC_FUNC)HwiF01, NULL); // 7 = PLIC_PRIO_LEVELS
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HWI_PRIO_INVALID, ret);
#else
    {
        HwiControllerOps *ops = LOS_HwiOpsGet();
        UINT32 (*origSetPri)(HWI_HANDLE_T, HWI_PRIOR_T) = ops->setIrqPriority;
        HWI_PRIOR_T hwiPrio = 3; // 3, valid priority.

        /* F-Inv: setIrqPriority 失败桩 → LOS_HwiCreate 走 OsHwiDel 回退分支 */
        ops->setIrqPriority = HwiSetPriorityFail;
        ret = LOS_HwiCreate(HWI_NUM_TEST, hwiPrio, mode, (HWI_PROC_FUNC)HwiF01, NULL);
        ops->setIrqPriority = origSetPri; /* 先恢复 ops 再断言，保证异常路径也不污染全局 */
        ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);
    }
#endif

    /* F-Pos: 失败回退已清理表项，同号再创建应成功（证明无资源泄漏残留） */
    ret = LOS_HwiCreate(HWI_NUM_TEST, 3, mode, (HWI_PROC_FUNC)HwiF01, NULL); // 3, valid priority.
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiDisable(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    TestHwiDelete(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(g_testCount, 0, g_testCount); // 全程未派发真实中断

    return LOS_OK;
}

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi051
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_HwiCreate setIrqPriority failure rollback path
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: (ws63) LOS_HwiCreate(HWI_NUM_TEST, 7, ...) out-of-range priority;
 *        (qemu) swap ops->setIrqPriority to a failing stub via LOS_HwiOpsGet
 * step2: re-create the same irq num with valid priority (proves rollback cleanup)
 * step3: delete the irq and verify no real interrupt dispatched
 * @par TestCase_Expected_Result
 * 1. first create fails (ws63: LOS_ERRNO_HWI_PRIO_INVALID / qemu stub: LOS_NOK)
 * 2. second create returns LOS_OK (no ALREADY_CREATED residue)
 * 3. g_testCount stays 0 (no real IRQ dispatched)
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */
VOID ItLosHwi051(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHwi051", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL1, TEST_FUNCTION);
}

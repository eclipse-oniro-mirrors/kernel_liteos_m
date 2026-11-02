/*
 * Copyright (c) 2022-2022 Huawei Device Co., Ltd. All rights reserved.
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "osTest.h"
#include "it_los_hwi.h"

/* 仅 qemu(cortex-m NVIC): it_los_hwi_040 换桩后真驱动 HwiClear 从未执行 */
#if defined(LOSCFG_ARCH_ARM) && defined(LOSCFG_CORTEX_M_NVIC)

static VOID HwiF052(VOID)
{
    g_testCount++;
}

/* 用例简要描述: 真驱动下HwiClear清pending位成功 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    HWI_MODE_T mode = 0;
    HWI_PRIOR_T hwiPrio = 3; /* 3, valid priority. */

    g_testCount = 0;
    /* 前置防御: 清理 HWI_NUM_TEST 可能的历史残留 */
    (VOID)TestHwiDelete(HWI_NUM_TEST);

    /* F-Pos: 真实 ops 表(不换桩)下创建中断 */
    ret = LOS_HwiCreate(HWI_NUM_TEST, hwiPrio, mode, (HWI_PROC_FUNC)HwiF052, NULL);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 使能后软触发 -> Cortex-M pending 位立即派发, ISR 计数一次
     * (真驱动 HwiPending, arm_nvic.c:72-79) */
    ret = LOS_HwiEnable(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_HwiTrigger(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT); /* compare with the expect value 1 */

    /* F-Pos: 真驱动 LOS_HwiClear 清 ICPR pending 位(arm_nvic.c:86-87) */
    ret = LOS_HwiClear(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    /* F-Pos: 掩蔽后软触发只 pending 不派发, 计数不变 */
    ret = LOS_HwiDisable(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_HwiTrigger(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT); /* still 1: masked line */

    /* F-Pos: 真驱动 HwiClear 清掉掩蔽期 pending; 再使能不派发, 计数不变 */
    ret = LOS_HwiClear(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = LOS_HwiEnable(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ICUNIT_GOTO_EQUAL(g_testCount, 1, g_testCount, EXIT); /* clear worked: no dispatch on re-enable */

    /* 清理: 掩蔽 + 删除, 无资源残留 */
    ret = LOS_HwiDisable(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    ret = TestHwiDelete(HWI_NUM_TEST);
    ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);

    return LOS_OK;

EXIT:
    (VOID)LOS_HwiDisable(HWI_NUM_TEST);
    (VOID)TestHwiDelete(HWI_NUM_TEST);
    return LOS_OK;
}

#endif /* LOSCFG_ARCH_ARM && LOSCFG_CORTEX_M_NVIC */

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi052
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_HwiCreate/LOS_HwiTrigger/LOS_HwiClear/LOS_HwiDelete with real ops
 * @par TestCase_Pretreatment_Condition
 * ARM Cortex-M NVIC board (qemu).
 * @par TestCase_Test_Steps
 * step1: create HWI_NUM_TEST with real controller ops (no stub swap).
 * step2: enable + trigger -> handler runs once (real HwiPending path).
 * step3: LOS_HwiClear -> LOS_OK (real driver ICPR clear, arm_nvic.c:86-87).
 * step4: disable + trigger -> pending only; LOS_HwiClear; re-enable -> no dispatch.
 * step5: delete the irq, no residue.
 * @par TestCase_Expected_Result
 * 1.All LOS_Hwi* calls return LOS_OK.
 * 2.g_testCount==1 after dispatch, stays 1 while masked, stays 1 after clear+re-enable.
 * @par TestCase_Level
 * Level 0
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */
VOID ItLosHwi052(VOID) // IT_Layer_ModuleORFeature_No
{
#if defined(LOSCFG_ARCH_ARM) && defined(LOSCFG_CORTEX_M_NVIC)
    TEST_ADD_CASE("ItLosHwi052", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

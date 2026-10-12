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

/* 仅 ws63(himideerv200 PLIC): 高段向量 32..95 的 LOCIEN0/1 与 LOCIPRI reg1-8 */
#if defined(LOSCFG_ARCH_RISCV) && defined(LOSCFG_RISCV_HIMIDEERV200_PLIC)

/* 高段使能/屏蔽代表向量: 40/56(LOCIEN0, 32..63), 88(LOCIEN1, 64..95) */
STATIC const UINT32 g_hwiVec[] = {40, 56, 88};
/* LOCIPRI reg1-8 代表向量: (n-26)/8 = 1..8 */
STATIC const UINT32 g_hwiPrioVec[] = {34, 42, 50, 58, 66, 74, 82, 90};
#define HWI_TC053_VEC_NUM      3
#define HWI_TC053_PRIO_VEC_NUM 8
#define HWI_TC053_PRIO_MAX     6 /* PLIC logical priorities 0..6 */

/* 用例简要描述: 高段向量使能屏蔽与优先级读写 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 i;
    UINT32 prio;

    /* F-Pos: 高段向量 Enable/Disable(只写使能/屏蔽位, 无真实中断源不产生 pending,
     * 覆盖 HalIrqUnmask LOCIEN0(63-64)/LOCIEN1(66) 与 HalIrqMask(78-79/81)) */
    for (i = 0; i < HWI_TC053_VEC_NUM; i++) {
        ret = LOS_HwiEnable(g_hwiVec[i]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        ret = LOS_HwiDisable(g_hwiVec[i]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    }

    /* F-Pos/F-Inv: LOCIPRI reg1-8 代表向量逐个 SetPriority, 优先级 0..6 合法档
     * 逐档验证(覆盖 PlicLocipriRead/Write case1-8, 107-114/129-136) */
    for (i = 0; i < HWI_TC053_PRIO_VEC_NUM; i++) {
        for (prio = 0; prio <= HWI_TC053_PRIO_MAX; prio++) {
            ret = LOS_HwiSetPriority(g_hwiPrioVec[i], (HWI_PRIOR_T)prio);
            ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
        }
    }

    /* 现场恢复: 高段向量全部 Disable 回写(优先级字段无真实中断源, 无需回写) */
    for (i = 0; i < HWI_TC053_VEC_NUM; i++) {
        ret = LOS_HwiDisable(g_hwiVec[i]);
        ICUNIT_GOTO_EQUAL(ret, LOS_OK, ret, EXIT);
    }

    return LOS_OK;

EXIT:
    for (i = 0; i < HWI_TC053_VEC_NUM; i++) {
        (VOID)LOS_HwiDisable(g_hwiVec[i]);
    }
    return LOS_OK;
}

#endif /* LOSCFG_ARCH_RISCV && LOSCFG_RISCV_HIMIDEERV200_PLIC */

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi053
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_HwiEnable/LOS_HwiDisable/LOS_HwiSetPriority on high PLIC vectors
 * @par TestCase_Pretreatment_Condition
 * ws63 himideerv200 PLIC board (riscv).
 * @par TestCase_Test_Steps
 * step1: enable/disable vectors 40/56 (LOCIEN0) and 88 (LOCIEN1).
 * step2: LOS_HwiSetPriority on LOCIPRI reg1-8 representative vectors
 *        {34,42,50,58,66,74,82,90} for every valid priority 0..6.
 * step3: disable all touched vectors to restore.
 * @par TestCase_Expected_Result
 * 1.All calls return LOS_OK; no real interrupt is dispatched (no handler registered).
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */
VOID ItLosHwi053(VOID) // IT_Layer_ModuleORFeature_No
{
#if defined(LOSCFG_ARCH_RISCV) && defined(LOSCFG_RISCV_HIMIDEERV200_PLIC)
    TEST_ADD_CASE("ItLosHwi053", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL1, TEST_FUNCTION);
#endif
}

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

/* 用例简要描述: LOS_HwiTrigger（对外接口）软触发中断正/负路径——
 * 非法中断号（>OS_HWI_MAX_NUM）负路径；合法中断号正路径：
 * ws63 himideerv200 PLIC 无软触发 CSR，恒返回 LOS_NOK；
 * qemu NVIC 走 pending 位（先掩蔽+清 pending，确保不派发真实中断） */
static UINT32 Testcase(VOID)
{
    UINT32 ret;

    /* F-Inv: 中断号超出 OS_HWI_MAX_NUM */
    ret = LOS_HwiTrigger(OS_HWI_MAX_NUM + 1);
#ifdef LOSCFG_PLATFORM_WS63_M
    /* F-Inv: ws63 软触发不支持，任意参数返回 LOS_NOK */
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);

    /* F-Pos: 合法中断号，返回 LOS_NOK 且无副作用 */
    ret = LOS_HwiTrigger(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(ret, LOS_NOK, ret);
#else
    /* F-Inv: qemu NVIC HwiPending 边界检查拦截 */
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HWI_NUM_INVALID, ret);

    /* F-Pos: 先掩蔽中断线并清残留 pending，软触发只置 pending 位不派发 */
    ret = LOS_HwiDisable(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    TestHwiClear(HWI_NUM_TEST);
    ret = LOS_HwiTrigger(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    TestHwiClear(HWI_NUM_TEST);
#endif

    return LOS_OK;
}

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi049
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_HwiTrigger
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: call LOS_HwiTrigger(OS_HWI_MAX_NUM + 1) negative path
 * step2: call LOS_HwiTrigger(HWI_NUM_TEST) positive path (masked line, no dispatch)
 * @par TestCase_Expected_Result
 * 1. ws63: both calls return LOS_NOK (no software-pending CSR)
 * 2. qemu: invalid num returns LOS_ERRNO_HWI_NUM_INVALID, valid num returns LOS_OK
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */
VOID ItLosHwi049(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHwi049", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL1, TEST_FUNCTION);
}

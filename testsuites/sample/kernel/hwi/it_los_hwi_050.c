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

/* 用例简要描述: LOS_HwiEnable/LOS_HwiSetPriority/LOS_HwiCurIrqNum（对外接口）正/负路径——
 * 非法中断号（>OS_HWI_MAX_NUM）负路径；ws63 PLIC 优先级越界（>=7）负路径与
 * 本地向量（<26 无 LOCIPRI 字段）静默成功；合法参数成功后掩蔽恢复现场；
 * 任务上下文调用 LOS_HwiCurIrqNum 查询当前中断号 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 irqNum;
    HWI_PRIOR_T hwiPrio = 3; // 3, valid priority.

    /* F-Inv: 中断号超出 OS_HWI_MAX_NUM */
    ret = LOS_HwiEnable(OS_HWI_MAX_NUM + 1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HWI_NUM_INVALID, ret);
    ret = LOS_HwiSetPriority(OS_HWI_MAX_NUM + 1, hwiPrio);
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HWI_NUM_INVALID, ret);

#ifdef LOSCFG_PLATFORM_WS63_M
    /* F-Inv: ws63 PLIC 优先级有效范围 0..6，7 越界返回 LOS_ERRNO_HWI_PRIO_INVALID */
    ret = LOS_HwiSetPriority(HWI_NUM_TEST, 7); // 7 = PLIC_PRIO_LEVELS, out of range.
    ICUNIT_ASSERT_EQUAL(ret, LOS_ERRNO_HWI_PRIO_INVALID, ret);

    /* F-Pos: 向量号 <26 无 LOCIPRI 字段，驱动静默返回 LOS_OK */
    ret = LOS_HwiSetPriority(OS_USER_HWI_MIN, 1);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
#endif

    /* F-Pos: 合法中断号设置优先级成功 */
    ret = LOS_HwiSetPriority(HWI_NUM_TEST, hwiPrio);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Pos: 使能后立即掩蔽恢复，避免残留使能状态（先清残留 pending 防误派发） */
    TestHwiClear(HWI_NUM_TEST);
    ret = LOS_HwiEnable(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = LOS_HwiDisable(HWI_NUM_TEST);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    /* F-Pos: 任务上下文查询当前中断号（qemu 线程模式 IPSR=0） */
    irqNum = LOS_HwiCurIrqNum();
#ifdef LOSCFG_PLATFORM_WS63_M
    /* ws63 任务态读 mcause 残留值无确定语义，仅验证接口路径可执行 */
    (VOID)irqNum;
#else
    ICUNIT_ASSERT_EQUAL(irqNum, 0, irqNum);
#endif

    return LOS_OK;
}

/**
 * @ingroup TEST_HWI
 * @par TestCase_Number
 * ItLosHwi050
 * @par TestCase_TestCase_Type
 * Function test
 * @brief Test interface LOS_HwiEnable, LOS_HwiSetPriority, LOS_HwiCurIrqNum
 * @par TestCase_Pretreatment_Condition
 * NA.
 * @par TestCase_Test_Steps
 * step1: call LOS_HwiEnable/LOS_HwiSetPriority with OS_HWI_MAX_NUM + 1
 * step2: (ws63) LOS_HwiSetPriority(HWI_NUM_TEST, 7) out-of-range priority;
 *        LOS_HwiSetPriority(OS_USER_HWI_MIN, 1) silent-success local vector
 * step3: valid LOS_HwiSetPriority/LOS_HwiEnable then LOS_HwiDisable restore
 * step4: call LOS_HwiCurIrqNum in task context
 * @par TestCase_Expected_Result
 * 1. invalid irq num returns LOS_ERRNO_HWI_NUM_INVALID
 * 2. ws63 prio 7 returns LOS_ERRNO_HWI_PRIO_INVALID; local vector returns LOS_OK
 * 3. valid calls return LOS_OK; qemu CurIrqNum returns 0 (thread mode IPSR)
 * @par TestCase_Level
 * Level 1
 * @par TestCase_Automated
 * true
 * @par TestCase_Remark
 * null
 */
VOID ItLosHwi050(VOID) // IT_Layer_ModuleORFeature_No
{
    TEST_ADD_CASE("ItLosHwi050", Testcase, TEST_LOS, TEST_HWI, TEST_LEVEL1, TEST_FUNCTION);
}

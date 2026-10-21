/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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

#include "it_los_misc.h"

#if defined(LOSCFG_PLATFORM_WS63_M)
/* LOS_SetSysClosk/LOS_GetSysClosk 仅 ws63 板级 board.c:16/21 定义(历史拼写
 * Closk), 无公共头文件声明(全仓库仅定义), 按设计(ws63_board_test_design
 * §5.1 注)在用例内 extern 原型声明, 不引私有头 */

/* 用例简要描述: 系统时钟设值回读并恢复原值
 * F-Pos+BVA: 取 orig=LOS_GetSysClosk() -> 对采样值 v ∈ {0, OS_SYS_CLOCK,
 * 0xFFFFFFFF}: LOS_SetSysClosk(v) 后断言 LOS_GetSysClosk()==v(GOTO EXIT
 * 变体保证任一断言失败也走恢复); EXIT 无条件 LOS_SetSysClosk(orig) 并断言
 * 回读==orig。gSysClock 全仓库仅本 getter 读取(改值无内核副作用, 缺口分析
 * §7 源码核实), 双保险恢复不残留。板级: 仅 ws63(接口仅该板定义, qemu 板无
 * 此符号), gate LOSCFG_PLATFORM_WS63_M(接口仅 ws63 board.c 定义, hi3322 无此符号)。 */
extern VOID LOS_SetSysClosk(UINT32 clock);
extern UINT32 LOS_GetSysClosk(VOID);

static UINT32 TestCase(VOID)
{
    UINT32 orig;
    UINT32 testVal[] = {0, OS_SYS_CLOCK, 0xFFFFFFFF};
    UINT32 i;
    UINT32 count = sizeof(testVal) / sizeof(testVal[0]);

    /* 前置: 记录原值(初值 OS_SYS_CLOCK, board.c:14) */
    orig = LOS_GetSysClosk();

    for (i = 0; i < count; i++) {
        LOS_SetSysClosk(testVal[i]);
        ICUNIT_GOTO_EQUAL(LOS_GetSysClosk(), testVal[i], testVal[i], EXIT);
    }

EXIT:
    /* 清理(无条件): 恢复原值并回读确认 */
    LOS_SetSysClosk(orig);
    ICUNIT_ASSERT_EQUAL(LOS_GetSysClosk(), orig, orig);
    return LOS_OK;
}
#endif /* LOSCFG_PLATFORM_WS63_M */

VOID ItLosSysClock001(VOID)
{
#if defined(LOSCFG_PLATFORM_WS63_M)
    TEST_ADD_CASE("ItLosSysClock001", TestCase, TEST_LOS, TEST_MISC, TEST_LEVEL0, TEST_FUNCTION);
#endif
}

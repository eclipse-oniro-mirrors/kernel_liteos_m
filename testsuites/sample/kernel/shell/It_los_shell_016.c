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
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "It_los_shell.h"
#include "shcmd.h"

#if defined(LOSCFG_SHELL)

/* 用例简要描述: free/memusage 命令全参数分支直调: 无参(字节)/-k(KB)/-m(MB)/
 * 非法选项(Usage)/参数超限,覆盖 OsShellCmdFreeInfo 与 OsShellCmdWaterLine 全部分支 */
static UINT32 Testcase(VOID)
{
    UINT32 ret;
    const CHAR *argvK[1] = {"-k"};
    const CHAR *argvM[1] = {"-m"};
    const CHAR *argvX[1] = {"-x"};
    const CHAR *argv2[2] = {"free", "-k"};

    /* F-Pos: free 无参(字节数输出) */
    ret = OsShellCmdFree(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Pos: free -k(KB 输出) */
    ret = OsShellCmdFree(1, argvK);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Pos: free -m(MB 输出) */
    ret = OsShellCmdFree(1, argvM);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Inv: free 非法选项(Usage) */
    ret = OsShellCmdFree(1, argvX);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)OS_ERROR, ret);

    /* F-Inv: free 参数超限 */
    ret = OsShellCmdFree(2, argv2);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)OS_ERROR, ret);

#if (LOSCFG_MEM_WATERLINE == 1)
    /* F-Pos: memusage 无参(水位字节输出) */
    ret = OsShellCmdWaterLine(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Pos: memusage -k(KB 输出) */
    ret = OsShellCmdWaterLine(1, argvK);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Pos: memusage -m(MB 输出) */
    ret = OsShellCmdWaterLine(1, argvM);
    ICUNIT_ASSERT_EQUAL(ret, 0, ret);

    /* F-Inv: memusage 非法选项(Usage) */
    ret = OsShellCmdWaterLine(1, argvX);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)OS_ERROR, ret);

    /* F-Inv: memusage 参数超限 */
    ret = OsShellCmdWaterLine(2, argv2);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)OS_ERROR, ret);
#endif

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell016
 * @tc.desc     验证 free/memusage 命令对外接口全参数分支: 无参字节输出、-k KB、
 *              -m MB、非法选项 Usage 错误、参数个数超限错误; memusage 部分受
 *              LOSCFG_MEM_WATERLINE 守卫。双板均可运行
 */
VOID ItLosShell016(VOID)
{
    TEST_ADD_CASE("ItLosShell016", Testcase, TEST_LOS, TEST_SHELL_MOD, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */

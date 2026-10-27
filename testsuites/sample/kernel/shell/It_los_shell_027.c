/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
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

#ifdef LOSCFG_SHELL
#if (LOSCFG_USE_SHELL == 1)

static UINT32 Testcase(VOID)
{
    UINT32 ret;

    ret = OsShellInit();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = OsShellCmdWatch(0, NULL);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)OS_ERROR, ret);

    const CHAR *argvOver[1] = {"--over"};
    ret = OsShellCmdWatch(1, argvOver);
    ICUNIT_ASSERT_EQUAL(ret, (UINT32)OS_ERROR, ret);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell027
 * @tc.desc     验证 watch 命令: 无参返回 OS_ERROR(打印 Usage);
 *              --over 无运行中的 watch 任务返回 OS_ERROR
 */
VOID ItLosShell027(VOID)
{
    TEST_ADD_CASE("ItLosShell027", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_USE_SHELL == 1 */
#endif /* LOSCFG_SHELL */

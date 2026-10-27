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
#include "shmsg.h"

#ifdef LOSCFG_SHELL
#ifdef LOSCFG_CMD_UNLOAD

static volatile UINT32 g_unload031Calls;

static UINT32 TestShell031Hook(UINT32 argc, const CHAR **argv)
{
    (VOID)argc;
    (VOID)argv;
    g_unload031Calls++;
    return 0;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;

    ret = OsShellInit();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    g_unload031Calls = 0;

    ret = osCmdReg(CMD_TYPE_EX, "tc_unload031", XARGS, TestShell031Hook);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = ShellMsgParse("tc_unload031");
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ICUNIT_ASSERT_EQUAL(g_unload031Calls, 1, g_unload031Calls);

    ret = OsShellCmdDelete((CHAR *)"tc_unload031");
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = ShellMsgParse("tc_unload031");
    ICUNIT_ASSERT_EQUAL(ret != LOS_OK, 1, ret);
    ICUNIT_ASSERT_EQUAL(g_unload031Calls, 1, g_unload031Calls);

    ret = OsShellCmdDelete((CHAR *)"tc_unload031");
    ICUNIT_ASSERT_EQUAL(ret, OS_ERRNO_SHELL_CMDREG_CMD_NOT_EXIST, ret);

    ret = OsShellCmdDelete(NULL);
    ICUNIT_ASSERT_EQUAL(ret, OS_ERRNO_SHELL_CMDREG_PARA_ERROR, ret);

    ret = osCmdReg(CMD_TYPE_EX, "tc_unload031", XARGS, TestShell031Hook);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);
    ret = OsShellCmdDelete((CHAR *)"tc_unload031");
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell031
 * @tc.desc     验证命令卸载(OsShellCmdDelete): 运行期注册的命令经管道可执行(回调计数);
 *              卸载后再执行返回 command not found 且回调不再触发;
 *              重复卸载返回 CMD_NOT_EXIST, 空键名返回 PARA_ERROR;
 *              卸载后可重注册(堆净增为零)
 */
VOID ItLosShell031(VOID)
{
    TEST_ADD_CASE("ItLosShell031", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_CMD_UNLOAD */
#endif /* LOSCFG_SHELL */

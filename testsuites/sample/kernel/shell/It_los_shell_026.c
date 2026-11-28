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
#include "shell_pri.h"

#ifdef LOSCFG_SHELL

static BOOL ShellCmdFindInList(const CHAR *cmdKey)
{
    CmdItemNode *curCmdItem = NULL;
    CmdModInfo *cmdInfo = OsCmdInfoGet();
    UINT32 len;

    if ((cmdInfo == NULL) || (cmdKey == NULL)) {
        return FALSE;
    }

    len = (UINT32)strlen(cmdKey);
    LOS_DL_LIST_FOR_EACH_ENTRY(curCmdItem, &cmdInfo->cmdList.list, CmdItemNode, list) {
        if ((len == strlen(curCmdItem->cmd->cmdKey)) &&
            (strncmp(curCmdItem->cmd->cmdKey, cmdKey, len) == 0)) {
            return TRUE;
        }
    }
    return FALSE;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    UINT32 count;
    BOOL found;

    ret = OsShellInit();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    count = ((UINTPTR)(&g_shellcmdEnd) - (UINTPTR)(&g_shellcmdBegin)) / (UINT32)sizeof(CmdItem);
    ICUNIT_ASSERT_EQUAL(count > 0, 1, count);

    found = ShellCmdFindInList("date");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);

    found = ShellCmdFindInList("task");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);

    found = ShellCmdFindInList("free");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);

    found = ShellCmdFindInList("help");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);

    found = ShellCmdFindInList("memcheck");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);

    found = ShellCmdFindInList("systeminfo");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);

    found = ShellCmdFindInList("md");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);

    found = ShellCmdFindInList("mw");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);

#if (LOSCFG_USE_SHELL == 1)
    found = ShellCmdFindInList("watch");
    ICUNIT_ASSERT_EQUAL(found, TRUE, found);
#endif

    found = ShellCmdFindInList("nosuchcmd_zzz");
    ICUNIT_ASSERT_EQUAL(found, FALSE, found);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell026
 * @tc.desc     验证 SHELLCMD_ENTRY 链接段表注册机制: 段表元素数 > 0;
 *              date/task/free/help/memcheck/systeminfo/md/mw/watch 均在注册表中可找到;
 *              不存在的命令名返回 FALSE
 */
VOID ItLosShell026(VOID)
{
    TEST_ADD_CASE("ItLosShell026", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */

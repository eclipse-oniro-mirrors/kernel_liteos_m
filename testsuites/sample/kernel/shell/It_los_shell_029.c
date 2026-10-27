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
#include "shell_pri.h"
#include "los_memory.h"
#include "securec.h"

#ifdef LOSCFG_SHELL

static VOID TestFreeInputNode(CHAR *buf)
{
    CmdKeyLink *node = LOS_DL_LIST_ENTRY(buf, CmdKeyLink, cmdString);
    (VOID)LOS_MemFree(m_aucSysMem0, node);
}

static BOOL TestKeyLinkIsEmpty(const ShellCB *shellCB, CmdKeyLink *cmdKeyLink)
{
    BOOL isEmpty;

    KEY_LOCK(shellCB);
    isEmpty = LOS_ListEmpty(&cmdKeyLink->list);
    KEY_UNLOCK(shellCB);
    return isEmpty;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    BOOL isEmpty;
    ShellCB *shellCB;
    CmdKeyLink *cmdKeyLink;
    CmdKeyLink *cmdHistoryLink;
    CHAR *buf;

    ret = OsShellInit();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    shellCB = OsGetShellCb();
    ICUNIT_ASSERT_EQUAL(shellCB != NULL, 1, 0);

    cmdKeyLink = (CmdKeyLink *)shellCB->cmdKeyLink;
    cmdHistoryLink = (CmdKeyLink *)shellCB->cmdHistoryKeyLink;
    ICUNIT_ASSERT_EQUAL(cmdKeyLink != NULL, 1, 0);
    ICUNIT_ASSERT_EQUAL(cmdHistoryLink != NULL, 1, 0);

    while (1) {
        buf = ShellGetInputBuf(shellCB);
        if (buf == NULL) {
            break;
        }
        TestFreeInputNode(buf);
    }

    KEY_LOCK(shellCB);
    OsShellCmdPush("task", cmdKeyLink);
    KEY_UNLOCK(shellCB);
    isEmpty = TestKeyLinkIsEmpty(shellCB, cmdKeyLink);
    ICUNIT_ASSERT_EQUAL(isEmpty, FALSE, TRUE);

    buf = ShellGetInputBuf(shellCB);
    ICUNIT_ASSERT_EQUAL(buf != NULL, 1, 0);
    ICUNIT_ASSERT_EQUAL(strncmp(buf, "task", 4), 0, 0);

    isEmpty = TestKeyLinkIsEmpty(shellCB, cmdKeyLink);
    ICUNIT_ASSERT_EQUAL(isEmpty, TRUE, FALSE);
    TestFreeInputNode(buf);

    KEY_LOCK(shellCB);
    OsShellCmdPush("free", cmdKeyLink);
    OsShellCmdPush("help", cmdKeyLink);
    KEY_UNLOCK(shellCB);
    isEmpty = TestKeyLinkIsEmpty(shellCB, cmdKeyLink);
    ICUNIT_ASSERT_EQUAL(isEmpty, FALSE, TRUE);

    buf = ShellGetInputBuf(shellCB);
    ICUNIT_ASSERT_EQUAL(buf != NULL, 1, 0);
    ICUNIT_ASSERT_EQUAL(strncmp(buf, "free", 4), 0, 0);
    TestFreeInputNode(buf);

    buf = ShellGetInputBuf(shellCB);
    ICUNIT_ASSERT_EQUAL(buf != NULL, 1, 0);
    ICUNIT_ASSERT_EQUAL(strncmp(buf, "help", 4), 0, 0);
    TestFreeInputNode(buf);

    buf = ShellGetInputBuf(shellCB);
    ICUNIT_ASSERT_EQUAL(buf == NULL, 1, 0);

    KEY_LOCK(shellCB);
    OsShellCmdPush("task", cmdKeyLink);
    KEY_UNLOCK(shellCB);

    buf = ShellGetInputBuf(shellCB);
    ICUNIT_ASSERT_EQUAL(buf != NULL, 1, 0);
    TestFreeInputNode(buf);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell029
 * @tc.desc     验证命令历史链表: OsShellCmdPush(持锁)写入后链表非空;
 *              ShellGetInputBuf 按 FIFO 取出字符串内容匹配且节点由测试释放;
 *              链表空后 ShellGetInputBuf 返回 NULL
 */
VOID ItLosShell029(VOID)
{
    TEST_ADD_CASE("ItLosShell029", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */

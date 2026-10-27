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
#include "los_task.h"
#include "los_memory.h"

#ifdef LOSCFG_SHELL

#define SHELL030_WAIT_LOOPS  200
#define SHELL030_WAIT_TICKS  5
#define SHELL030_FREE_MARGIN 512

static VOID TestShell030RunCmd(ShellCB *shellCB, const CHAR *cmd)
{
    CmdKeyLink *cmdKeyLink = (CmdKeyLink *)shellCB->cmdKeyLink;

    KEY_LOCK(shellCB);
    OsShellCmdPush(cmd, cmdKeyLink);
    KEY_UNLOCK(shellCB);
    (VOID)LOS_EventWrite(&shellCB->shellEvent, SHELL_CMD_PARSE_EVENT);
}

static BOOL TestShell030QueueEmpty(ShellCB *shellCB)
{
    CmdKeyLink *cmdKeyLink = (CmdKeyLink *)shellCB->cmdKeyLink;
    BOOL isEmpty;

    KEY_LOCK(shellCB);
    isEmpty = LOS_ListEmpty(&cmdKeyLink->list);
    KEY_UNLOCK(shellCB);
    return isEmpty;
}

static UINT32 TestShell030WaitDrain(ShellCB *shellCB)
{
    UINT32 loop;

    for (loop = 0; loop < SHELL030_WAIT_LOOPS; loop++) {
        (VOID)LOS_TaskDelay(SHELL030_WAIT_TICKS);
        if (TestShell030QueueEmpty(shellCB)) {
            return LOS_OK;
        }
    }
    return LOS_NOK;
}

/* The load commands land in the bounded history ring by design; clear it so
 * this case does not show up as a heap delta in the suite leak statistics. */
static VOID TestShell030ClearHistory(ShellCB *shellCB)
{
    CmdKeyLink *cmdHistory = (CmdKeyLink *)shellCB->cmdHistoryKeyLink;
    CmdKeyLink *node = NULL;

    HISTORY_LOCK(shellCB);
    while (!LOS_ListEmpty(&cmdHistory->list)) {
        node = LOS_DL_LIST_ENTRY(cmdHistory->list.pstNext, CmdKeyLink, list);
        LOS_ListDelete(&node->list);
        (VOID)LOS_MemFree(m_aucSysMem0, node);
    }
    cmdHistory->count = 0;
    shellCB->cmdMaskKeyLink = shellCB->cmdHistoryKeyLink;
    HISTORY_UNLOCK(shellCB);
}

static UINT32 TestShell030CheckTaskStack(UINT32 taskHandle, const CHAR *taskName)
{
    UINT32 ret;
    TSK_INFO_S taskInfo = {0};

    ret = LOS_TaskInfoGet(taskHandle, &taskInfo);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    PRINTK("\nItLosShell030: %s stack peak %u / %u, free margin %u\n",
           taskName, taskInfo.uwPeakUsed, taskInfo.uwStackSize,
           (taskInfo.uwStackSize > taskInfo.uwPeakUsed) ?
           (taskInfo.uwStackSize - taskInfo.uwPeakUsed) : 0);

    ICUNIT_ASSERT_EQUAL(taskInfo.bOvf, FALSE, taskInfo.bOvf);
    ICUNIT_ASSERT_EQUAL(taskInfo.uwPeakUsed < taskInfo.uwStackSize, 1, taskInfo.uwPeakUsed);
    ICUNIT_ASSERT_EQUAL(taskInfo.uwPeakUsed + SHELL030_FREE_MARGIN <= taskInfo.uwStackSize, 1,
                        taskInfo.uwPeakUsed);
    return LOS_OK;
}

static UINT32 Testcase(VOID)
{
    UINT32 ret;
    ShellCB *shellCB;

    ret = OsShellInit();
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    shellCB = OsGetShellCb();
    ICUNIT_ASSERT_EQUAL(shellCB != NULL, 1, 0);

    TestShell030RunCmd(shellCB, "help");
    TestShell030RunCmd(shellCB, "task");
    TestShell030RunCmd(shellCB, "free");
    TestShell030RunCmd(shellCB, "systeminfo");
#if (LOSCFG_USE_SHELL == 1)
    TestShell030RunCmd(shellCB, "watch --over");
#endif

    ret = TestShell030WaitDrain(shellCB);
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = TestShell030CheckTaskStack(shellCB->shellTaskHandle, "SerialShellTask");
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    ret = TestShell030CheckTaskStack(shellCB->shellEntryHandle, "SerialEntryTask");
    ICUNIT_ASSERT_EQUAL(ret, LOS_OK, ret);

    TestShell030ClearHistory(shellCB);

    return LOS_OK;
}

/*
 * @tc.name     ItLosShell030
 * @tc.desc     验证 shell 任务栈预算(F-06): 压载命令(help/task/free/systeminfo/watch --over)
 *              经真实 ShellTask 管道执行后, LOS_TaskInfoGet 测得 SerialShellTask/
 *              SerialEntryTask 栈峰值无溢出且余量 >= 512 字节, 并打印实测峰值
 */
VOID ItLosShell030(VOID)
{
    TEST_ADD_CASE("ItLosShell030", Testcase, TEST_LOS, TEST_FN_KERNELEXT_SHELL, TEST_LEVEL1, TEST_FUNCTION);
}

#endif /* LOSCFG_SHELL */

/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
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

#include "shmsg.h"
#include "stdlib.h"
#include "stdio.h"
#include "unistd.h"
#include "securec.h"
#include "los_task.h"
#include "los_event.h"
#include "los_list.h"
#include "los_printf.h"
#include "los_debug.h"
#include "los_memory.h"
#include "shell_pri.h"
#include "los_mux.h"
#if (LOSCFG_USE_SHELL == 1)
#include "uart.h"
#ifdef LOSCFG_ENABLE_MAGICKEY
#include "magickey.h"
#endif
#endif

#define SHELL_TASK_STACK_SIZE   LOSCFG_SHELL_STACK_SIZE
#define SHELL_ENTRY_STACK_SIZE  LOSCFG_SHELL_ENTRY_STACK_SIZE
#define SHELL_TASK_PRIORITY     LOSCFG_SHELL_PRIO
#define SHELL_ENTRY_PRIORITY    LOSCFG_SHELL_PRIO

#define SERIAL_SHELL_TASK_NAME "SerialShellTask"
#define SERIAL_ENTRY_TASK_NAME "SerialEntryTask"

/* Kept for backward compatibility with board UART drivers that write to this event.
 * The new shell architecture uses shellCB->shellEvent instead. */
EVENT_CB_S g_shellInputEvent;

enum {
    STAT_NORMAL_KEY,
    STAT_ESC_KEY,
    STAT_MULTI_KEY
};

enum {
    CMD_KEY_ESC_VALUE   = 0x1b,
    CMD_KEY_UP_VALUE    = 0x41,
    CMD_KEY_DOWN_VALUE  = 0x42,
    CMD_KEY_RIGHT_VALUE = 0x43,
    CMD_KEY_LEFT_VALUE  = 0x44,
    CMD_KEY_COMBINATION_VALUE = 0x5b
};

CHAR *ShellGetInputBuf(ShellCB *shellCB)
{
    CmdKeyLink *cmdKey = shellCB->cmdKeyLink;
    CmdKeyLink *cmdNode = NULL;

    KEY_LOCK(shellCB);
    if ((cmdKey == NULL) || LOS_ListEmpty(&cmdKey->list)) {
        KEY_UNLOCK(shellCB);
        return NULL;
    }

    cmdNode = LOS_DL_LIST_ENTRY(cmdKey->list.pstNext, CmdKeyLink, list);
    LOS_ListDelete(&(cmdNode->list));
    KEY_UNLOCK(shellCB);

    return cmdNode->cmdString;
}

STATIC VOID ShellSaveHistoryCmd(CHAR *string, ShellCB *shellCB)
{
    CmdKeyLink *cmdHistory = shellCB->cmdHistoryKeyLink;
    CmdKeyLink *cmdKey = LOS_DL_LIST_ENTRY(string, CmdKeyLink, cmdString);
    CmdKeyLink *cmdNxt = NULL;

    if ((string == NULL) || (strlen(string) == 0)) {
        return;
    }

    HISTORY_LOCK(shellCB);

    if (cmdHistory->count != 0) {
        cmdNxt = LOS_DL_LIST_ENTRY(cmdHistory->list.pstPrev, CmdKeyLink, list);
        if (strcmp(string, cmdNxt->cmdString) == 0) {
            (VOID)LOS_MemFree(m_aucSysMem0, (VOID *)cmdKey);
            goto END;
        }
    }

    if (cmdHistory->count == CMD_HISTORY_LEN) {
        cmdNxt = LOS_DL_LIST_ENTRY(cmdHistory->list.pstNext, CmdKeyLink, list);
        LOS_ListDelete(&cmdNxt->list);
        LOS_ListTailInsert(&cmdHistory->list, &cmdKey->list);
        (VOID)LOS_MemFree(m_aucSysMem0, (VOID *)cmdNxt);
        goto END;
    }

    LOS_ListTailInsert(&cmdHistory->list, &cmdKey->list);
    cmdHistory->count++;

END:
    HISTORY_UNLOCK(shellCB);
    return;
}

STATIC VOID ShellNotify(ShellCB *shellCB)
{
    (VOID)LOS_EventWrite(&shellCB->shellEvent, SHELL_CMD_PARSE_EVENT);
}

STATIC INT32 ShellCmdLineCheckUDRL(const CHAR ch, ShellCB *shellCB)
{
    INT32 ret = LOS_NOK;

    switch (ch) {
        case CMD_KEY_ESC_VALUE:
            shellCB->shellKeyType = STAT_ESC_KEY;
            ret = LOS_OK;
            break;
        case CMD_KEY_COMBINATION_VALUE:
            if (shellCB->shellKeyType == STAT_ESC_KEY) {
                shellCB->shellKeyType = STAT_MULTI_KEY;
                ret = LOS_OK;
            }
            break;
        case CMD_KEY_UP_VALUE:
            if (shellCB->shellKeyType == STAT_MULTI_KEY) {
                OsShellHistoryShow(CMD_KEY_UP, shellCB);
                shellCB->shellKeyType = STAT_NORMAL_KEY;
                ret = LOS_OK;
            }
            break;
        case CMD_KEY_DOWN_VALUE:
            if (shellCB->shellKeyType == STAT_MULTI_KEY) {
                shellCB->shellKeyType = STAT_NORMAL_KEY;
                OsShellHistoryShow(CMD_KEY_DOWN, shellCB);
                ret = LOS_OK;
            }
            break;
        case CMD_KEY_RIGHT_VALUE:
            /* fall-through */
        case CMD_KEY_LEFT_VALUE:
            if (shellCB->shellKeyType == STAT_MULTI_KEY) {
                shellCB->shellKeyType = STAT_NORMAL_KEY;
                ret = LOS_OK;
            }
            break;
        default:
            break;
    }
    return ret;
}

STATIC VOID ShellCmdDispatch(ShellCB *shellCB)
{
    KEY_LOCK(shellCB);
    OsShellCmdPush(shellCB->shellBuf, shellCB->cmdKeyLink);
    KEY_UNLOCK(shellCB);
    ShellNotify(shellCB);
}

STATIC VOID ShellCmdTab(ShellCB *shellCB, pf_OUTPUT outputFunc)
{
    INT32 ret;

    if ((shellCB->shellBufOffset > 0) && (shellCB->shellBufOffset < (SHOW_MAX_LEN - 1))) {
        ret = OsTabCompletion(shellCB->shellBuf, &shellCB->shellBufOffset);
        if (ret > 1) {
            outputFunc("OHOS # %s", shellCB->shellBuf);
        }
    }
}

LITE_OS_SEC_TEXT_MINOR VOID ShellCmdLineParse(CHAR c, pf_OUTPUT outputFunc, ShellCB *shellCB)
{
    const CHAR ch = c;
    INT32 ret;

    if ((shellCB->shellBufOffset == 0) && (ch != '\n') && (ch != '\0')) {
        (VOID)memset_s(shellCB->shellBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
    }

    if (ch == '\r') {
        if (shellCB->shellBufOffset < (SHOW_MAX_LEN - 1)) {
            shellCB->shellBuf[shellCB->shellBufOffset] = '\0';
        }
        shellCB->shellBufOffset = 0;
        ShellCmdDispatch(shellCB);
        return;
    } else if ((ch == '\b') || (ch == 0x7F)) { /* backspace or delete(0x7F) */
        if ((shellCB->shellBufOffset > 0) && (shellCB->shellBufOffset < (SHOW_MAX_LEN - 1))) {
            shellCB->shellBuf[shellCB->shellBufOffset - 1] = '\0';
            shellCB->shellBufOffset--;
            outputFunc("\b \b");
        }
        return;
    } else if (ch == 0x09) { /* 0x09: tab */
        ShellCmdTab(shellCB, outputFunc);
        return;
    }
    /* parse the up/down/right/left key */
    ret = ShellCmdLineCheckUDRL(ch, shellCB);
    if (ret == LOS_OK) {
        return;
    }

    if ((ch != '\n') && (ch != '\0')) {
        if (shellCB->shellBufOffset < (SHOW_MAX_LEN - 1)) {
            shellCB->shellBuf[shellCB->shellBufOffset] = ch;
        } else {
            shellCB->shellBuf[SHOW_MAX_LEN - 1] = '\0';
        }
        shellCB->shellBufOffset++;
        outputFunc("%c", ch);
    }

    shellCB->shellKeyType = STAT_NORMAL_KEY;
}

UINT32 ShellMsgTypeGet(CmdParsed *cmdParsed, const CHAR *cmdType)
{
    UINT32 minLen;
    CmdItemNode *curCmdItem = (CmdItemNode *)NULL;
    UINT32 len;
    CmdModInfo *cmdInfo = OsCmdInfoGet();

    if ((cmdParsed == NULL) || (cmdType == NULL) || (cmdInfo == NULL)) {
        return OS_INVALID;
    }

    len = (UINT32)strlen(cmdType);

    LOS_DL_LIST_FOR_EACH_ENTRY(curCmdItem, &cmdInfo->cmdList.list, CmdItemNode, list) {
        if ((len == strlen(curCmdItem->cmd->cmdKey)) &&
            (strncmp((CHAR *)(curCmdItem->cmd->cmdKey), cmdType, len) == 0)) {
            minLen = (len < CMD_KEY_LEN) ? len : (CMD_KEY_LEN - 1);
            if (strncpy_s((CHAR *)(cmdParsed->cmdKeyword), CMD_KEY_LEN, cmdType, minLen) != EOK) {
                PRINTK("strncpy_s failure in %s\n", __FUNCTION__);
            }
            cmdParsed->cmdType = curCmdItem->cmd->cmdType;
            return LOS_OK;
        }
    }

    return OS_INVALID;
}

STATIC UINT32 ShellMsgNameGetAndExec(CmdParsed *cmdParsed, const CHAR *output, UINT32 len)
{
    UINT32 loop;
    UINT32 ret;
    const CHAR *tmpStr = NULL;
    BOOL quotes = FALSE;
    CHAR *msgName = (CHAR *)LOS_MemAlloc(m_aucSysMem0, len + 1);
    if (msgName == NULL) {
        PRINTK("malloc failure in %s[%d]\n", __FUNCTION__, __LINE__);
        return OS_INVALID;
    }
    /* Scan the 'output' string for command */
    for (tmpStr = output, loop = 0; (*tmpStr != '\0') && (loop < len);) {
        if (*tmpStr == '\"') {
            SWITCH_QUOTES_STATUS(quotes);
            tmpStr++;
            continue;
        }
        if ((*tmpStr == ' ') && (QUOTES_STATUS_CLOSE(quotes))) {
            break;
        }
        msgName[loop] = *tmpStr++;
        loop++;
    }
    msgName[loop] = '\0';
    /* Scan the command list to check whether the command can be found */
    ret = ShellMsgTypeGet(cmdParsed, msgName);
    PRINTK("\n");
    if (ret != LOS_OK) {
        PRINTK("%s:command not found\n", msgName);
    } else {
        (VOID)OsCmdExec(cmdParsed, (CHAR *)output);
    }
    (VOID)LOS_MemFree(m_aucSysMem0, msgName);
    return ret;
}

LITE_OS_SEC_TEXT_MINOR UINT32 ShellMsgParse(const CHAR *msg)
{
    CHAR *output = NULL;
    UINT32 len;
    CmdParsed cmdParsed;
    UINT32 ret = OS_INVALID;

    if (msg == NULL) {
        goto END;
    }

    len = (UINT32)strlen(msg);
    output = (CHAR *)LOS_MemAlloc(m_aucSysMem0, len + 1);
    if (output == NULL) {
        PRINTK("malloc failure in %s[%d]\n", __FUNCTION__, __LINE__);
        goto END;
    }
    /* Call function 'OsCmdKeyShift' to squeeze and clear useless or overmuch space */
    ret = OsCmdKeyShift(msg, output, len + 1);
    if ((ret != LOS_OK) || (strlen(output) == 0)) {
        ret = OS_INVALID;
        goto END_FREE_OUTPUT;
    }

    (VOID)memset_s(&cmdParsed, sizeof(CmdParsed), 0, sizeof(CmdParsed));

    ret = ShellMsgNameGetAndExec(&cmdParsed, output, len);

END_FREE_OUTPUT:
    (VOID)LOS_MemFree(m_aucSysMem0, output);
END:
    return ret;
}

/* Backward compatibility: wrapper around ShellMsgParse */
CHAR *GetCmdName(const CHAR *cmdline, UINT32 len)
{
    UINT32 loop;
    const CHAR *tmpStr = NULL;
    BOOL quotes = FALSE;
    CHAR *cmdName = NULL;
    if (cmdline == NULL) {
        return NULL;
    }

    cmdName = (CHAR *)LOS_MemAlloc(m_aucSysMem0, len + 1);
    if (cmdName == NULL) {
        PRINTK("malloc failure in %s[%d]\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    for (tmpStr = cmdline, loop = 0; (*tmpStr != '\0') && (loop < len); ) {
        if (*tmpStr == '\"') {
            SWITCH_QUOTES_STATUS(quotes);
            tmpStr++;
            continue;
        }
        if ((*tmpStr == ' ') && (QUOTES_STATUS_CLOSE(quotes))) {
            break;
        }
        cmdName[loop] = *tmpStr++;
        loop++;
    }
    cmdName[loop] = '\0';

    return cmdName;
}

UINT32 PreHandleCmdline(const CHAR *input, CHAR **output, UINT32 *outputlen)
{
    UINT32 shiftLen;
    UINT32 ret;
    const CHAR *cmdBuf = input;
    UINT32 cmdBufLen = strlen(cmdBuf);
    CHAR *shiftStr = (CHAR *)LOS_MemAlloc(m_aucSysMem0, cmdBufLen + 1);

    if (shiftStr == NULL) {
        PRINTK("malloc failure in %s[%d]\n", __FUNCTION__, __LINE__);
        return SH_NOK;
    }
    (VOID)memset_s(shiftStr, cmdBufLen + 1, 0, cmdBufLen + 1);

    ret = OsCmdKeyShift(cmdBuf, shiftStr, cmdBufLen + 1);
    shiftLen = strlen(shiftStr);
    if ((ret != SH_OK) || (shiftLen == 0)) {
        ret = SH_NOK;
        goto END_FREE_SHIFTSTR;
    }
    *output = shiftStr;
    *outputlen = shiftLen;

    ret = SH_OK;
    goto END;

END_FREE_SHIFTSTR:
    (VOID)LOS_MemFree(m_aucSysMem0, shiftStr);
END:
    return ret;
}

STATIC VOID ParseAndExecCmdline(CmdParsed *cmdParsed, const CHAR *cmdline, UINT32 len)
{
    UINT32 ret;
    CHAR *cmdName = NULL;

    cmdName = GetCmdName(cmdline, len);
    if (cmdName == NULL) {
        PRINTK("malloc failure in %s[%d]\n", __FUNCTION__, __LINE__);
        return;
    }

    ret = ShellMsgTypeGet(cmdParsed, cmdName);
    if (ret != LOS_OK) {
        PRINTK("%s:command not found\n", cmdName);
        (VOID)LOS_MemFree(m_aucSysMem0, cmdName);
        return;
    }

    (VOID)OsCmdExec(cmdParsed, (CHAR *)cmdline);
    (VOID)LOS_MemFree(m_aucSysMem0, cmdName);
}

LITE_OS_SEC_TEXT_MINOR VOID ExecCmdline(const CHAR *cmdline)
{
    UINT32 ret;
    CHAR *output = NULL;
    UINT32 outputlen;
    CmdParsed cmdParsed;

    if (cmdline == NULL) {
        return;
    }

    (VOID)OsShellInit();

    /* strip out unnecessary characters */
    ret = PreHandleCmdline(cmdline, &output, &outputlen);
    if (ret == SH_NOK) {
        return;
    }

    (VOID)memset_s(&cmdParsed, sizeof(CmdParsed), 0, sizeof(CmdParsed));
    ParseAndExecCmdline(&cmdParsed, output, outputlen);
    (VOID)LOS_MemFree(m_aucSysMem0, output);
}

#if (LOSCFG_USE_SHELL == 1)
STATIC VOID ShellTaskIpcDeinit(ShellCB *shellCB);

STATIC VOID ShellExitProcess(ShellCB *shellCB)
{
    OsShellKeyDeInitShellCB(shellCB);
    ShellTaskIpcDeinit(shellCB);
    (VOID)LOS_MuxDelete(shellCB->historyMutex);
    (VOID)LOS_MuxDelete(shellCB->keyMutex);
    (VOID)LOS_MemFree(m_aucSysMem0, shellCB);
}

STATIC VOID ShellCmdProcess(ShellCB *shellCB)
{
    CHAR *buf = NULL;
    while (1) {
        buf = ShellGetInputBuf(shellCB);
        if (buf == NULL) {
            break;
        }
        (VOID)ShellMsgParse(buf);
        ShellSaveHistoryCmd(buf, shellCB);
        shellCB->cmdMaskKeyLink = shellCB->cmdHistoryKeyLink;
    }
}

LITE_OS_SEC_TEXT_MINOR VOID *ShellTask(UINT32 param)
{
    UINT32 ret;
    ShellCB *shellCB = (ShellCB *)(UINTPTR)param;

    while (1) {
        PRINTK("\nOHOS # ");
        ret = LOS_EventRead(&shellCB->shellEvent, SHELL_CMD_EVENT_MASK,
                            LOS_WAITMODE_OR | LOS_WAITMODE_CLR, LOS_WAIT_FOREVER);
        if (ret == SHELL_CMD_PARSE_EVENT) {
            ShellCmdProcess(shellCB);
        } else if (ret == SHELL_CMD_EXIT_EVENT) {
            break;
        }
    }
    ShellExitProcess(shellCB);
    return NULL;
}

LITE_OS_SEC_TEXT_MINOR VOID *ShellEntry(UINT32 param)
{
    ShellCB *shellCB = (ShellCB *)(UINTPTR)param;

    (VOID)memset_s(shellCB->shellBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
    return ShellStdinLoop(shellCB);
}

VOID *ShellStdinLoop(ShellCB *shellCB)
{
    INT32 ret;
    CHAR ch;

    while (1) {
        (VOID)LOS_EventRead(&g_shellInputEvent, 0x1, LOS_WAITMODE_AND | LOS_WAITMODE_CLR, LOS_WAIT_FOREVER);
        ret = UartGetc();
        if (ret <= 0) {
            continue;
        }
        ch = (CHAR)ret;
#ifdef LOSCFG_ENABLE_MAGICKEY
        if (CheckMagicKey(ch) == 1) {
            continue;
        }
#endif
        if (ch == CTRL_C) {
            PRINTK("^C\n\rOHOS # ");
            shellCB->shellBufOffset = 0;
            (VOID)memset_s(shellCB->shellBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
            continue;
        }
        ShellCmdLineParse(ch, (pf_OUTPUT)dprintf, shellCB);
    }
}

STATIC UINT32 ShellTaskIpcInit(ShellCB *shellCB)
{
    return LOS_EventInit(&shellCB->shellEvent);
}

STATIC VOID ShellTaskIpcDeinit(ShellCB *shellCB)
{
    (VOID)LOS_EventDestroy(&shellCB->shellEvent);
}

STATIC UINT32 ShellTaskInfoGet(ShellCB *shellCB, CHAR **name, TSK_ENTRY_FUNC *entry)
{
    (VOID)shellCB;
    /* in non-console mode, shell only support one input */
    *name = SERIAL_SHELL_TASK_NAME;
    *entry = ShellTask;
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 ShellTaskInit(ShellCB *shellCB)
{
    CHAR             *name = NULL;
    TSK_ENTRY_FUNC   taskEntry = NULL;
    TSK_INIT_PARAM_S initParam = {0};

    if (shellCB == NULL) {
        return LOS_NOK;
    }

    if (ShellTaskInfoGet(shellCB, &name, &taskEntry) != LOS_OK) {
        return LOS_NOK;
    }

    initParam.pfnTaskEntry = taskEntry;
    initParam.usTaskPrio   = SHELL_TASK_PRIORITY;
    initParam.uwStackSize  = SHELL_TASK_STACK_SIZE;
    initParam.pcName       = name;
    LOS_TASK_PARAM_INIT_ARG(initParam, shellCB);

    if (ShellTaskIpcInit(shellCB) != LOS_OK) {
        return LOS_NOK;
    }

    return LOS_TaskCreate(&shellCB->shellTaskHandle, &initParam);
}

LITE_OS_SEC_TEXT_MINOR VOID ShellTaskDeinit(ShellCB *shellCB)
{
    (VOID)LOS_TaskDelete(shellCB->shellEntryHandle);
    (VOID)LOS_EventWrite(&shellCB->shellEvent, SHELL_CMD_EXIT_EVENT);
}

LITE_OS_SEC_TEXT_MINOR UINT32 ShellEntryInit(ShellCB *shellCB)
{
    UINT32 ret;
    TSK_INIT_PARAM_S initParam = {0};

    if (shellCB == NULL) {
        return LOS_NOK;
    }

    /* in non-console mode, shell only support one input */
    initParam.pfnTaskEntry = (TSK_ENTRY_FUNC)ShellEntry;
    initParam.usTaskPrio   = SHELL_ENTRY_PRIORITY;
    initParam.uwStackSize  = SHELL_ENTRY_STACK_SIZE;
    initParam.pcName       = SERIAL_ENTRY_TASK_NAME;
    LOS_TASK_PARAM_INIT_ARG(initParam, shellCB);

    ret = LOS_TaskCreate(&shellCB->shellEntryHandle, &initParam);
    return ret;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LosShellInit(VOID)
{
    UINT32 ret;

    ret = LOS_EventInit(&g_shellInputEvent);
    if (ret != LOS_OK) {
        PRINTK("Init shellInputEvent failed! ERROR: 0x%x\n", ret);
        return ret;
    }

    return OsShellInit();
}
#endif /* LOSCFG_USE_SHELL */

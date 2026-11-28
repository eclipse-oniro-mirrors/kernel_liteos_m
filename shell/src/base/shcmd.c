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

#include "shcmd.h"
#include "show.h"
#include "shmsg.h"
#include "shell_pri.h"
#include "stdlib.h"
#include "unistd.h"
#ifdef LOSCFG_FS_VFS
#include "dirent.h"
#endif
#include "securec.h"
#include "los_mux.h"
#include "los_memory.h"
#include "los_sched_debug_pri.h"
#if (LOSCFG_USE_SHELL == 1)
#include "uart.h"
#endif

#if (LOSCFG_DEBUG_TOOLS == 1)
#include "los_debugtools.h"
#endif

#if (LOSCFG_KERNEL_TRACE == 1)
#include "los_trace_pri.h"
#endif

#define SHELL_INIT_MAGIC_FLAG 0xABABABAB

STATIC CmdModInfo cmdInfo;
ShellCB *g_shellCB = NULL;
STATIC UINT8 *g_cmdItemGroup = NULL;

LOS_HAL_TABLE_BEGIN(g_shellcmdBegin, shellcmd);
LOS_HAL_TABLE_END(g_shellcmdEnd, shellcmd);

SHELLCMD_ENTRY(date_shellcmd, CMD_TYPE_STD, "date", XARGS, (CmdCallBackFunc)OsShellCmdDate);
SHELLCMD_ENTRY(task_shellcmd, CMD_TYPE_EX, "task", 1, (CmdCallBackFunc)OsShellCmdDumpTask);
SHELLCMD_ENTRY(help_shellcmd, CMD_TYPE_EX, "help", 0, (CmdCallBackFunc)OsShellCmdHelp);
SHELLCMD_ENTRY(free_shellcmd, CMD_TYPE_EX, "free", XARGS, (CmdCallBackFunc)OsShellCmdFree);
SHELLCMD_ENTRY(memcheck_shellcmd, CMD_TYPE_EX, "memcheck", 0, (CmdCallBackFunc)OsShellCmdMemCheck);
SHELLCMD_ENTRY(systeminfo_shellcmd, CMD_TYPE_EX, "systeminfo", 1, (CmdCallBackFunc)OsShellCmdSystemInfo);
SHELLCMD_ENTRY(md_shellcmd, CMD_TYPE_STD, "md", 3, (CmdCallBackFunc)SystemMd);
SHELLCMD_ENTRY(mw_shellcmd, CMD_TYPE_STD, "mw", 2, (CmdCallBackFunc)SystemMw);

#if (LOSCFG_USE_SHELL == 1)
SHELLCMD_ENTRY(watch_shellcmd, CMD_TYPE_EX, "watch", XARGS, (CmdCallBackFunc)OsShellCmdWatch);
#endif

#ifdef LOSCFG_DEBUG_SWTMR
SHELLCMD_ENTRY(swtmr_shellcmd, CMD_TYPE_EX, "swtmr", 1, (CmdCallBackFunc)OsShellCmdSwtmrInfoGet);
#endif
#ifdef LOSCFG_DEBUG_MUTEX
SHELLCMD_ENTRY(mutex_shellcmd, CMD_TYPE_EX, "mutex", 0, (CmdCallBackFunc)OsShellCmdMuxInfoGet);
#endif
#ifdef LOSCFG_DEBUG_SEMAPHORE
SHELLCMD_ENTRY(sem_shellcmd, CMD_TYPE_EX, "sem", 1, (CmdCallBackFunc)OsShellCmdSemInfoGet);
#endif
#ifdef LOSCFG_DEBUG_QUEUE
SHELLCMD_ENTRY(queue_shellcmd, CMD_TYPE_EX, "queue", 0, (CmdCallBackFunc)OsShellCmdQueueInfoGet);
#endif
#ifdef LOSCFG_DEBUG_SCHED_STATISTICS
SHELLCMD_ENTRY(schedstatstart_shellcmd, CMD_TYPE_EX, "schedstatstart", 0, (CmdCallBackFunc)OsShellCmdSchedStatStart);
SHELLCMD_ENTRY(schedstatstop_shellcmd, CMD_TYPE_EX, "schedstatstop", 0, (CmdCallBackFunc)OsShellCmdSchedStatStop);
SHELLCMD_ENTRY(schedstatinfo_shellcmd, CMD_TYPE_EX, "schedstatinfo", 0, (CmdCallBackFunc)OsShellCmdSchedStatInfo);
#endif
#if (LOSCFG_MEM_WATERLINE == 1)
SHELLCMD_ENTRY(memusage_shellcmd, CMD_TYPE_EX, "memusage", XARGS, (CmdCallBackFunc)OsShellCmdWaterLine);
#endif
#ifdef LWIP_SHELLCMD_ENABLE
SHELLCMD_ENTRY(ifconfig_shellcmd, CMD_TYPE_EX, "ifconfig", XARGS, (CmdCallBackFunc)lwip_ifconfig);
SHELLCMD_ENTRY(ping_shellcmd, CMD_TYPE_EX, "ping", XARGS, (CmdCallBackFunc)OsShellPing);
#endif
#if (LOSCFG_FS_VFS == 1)
SHELLCMD_ENTRY(touch_shellcmd, CMD_TYPE_EX, "touch", XARGS, (CmdCallBackFunc)OsShellCmdTouch);
SHELLCMD_ENTRY(ls_shellcmd, CMD_TYPE_EX, "ls", XARGS, (CmdCallBackFunc)OsShellCmdLs);
SHELLCMD_ENTRY(pwd_shellcmd, CMD_TYPE_EX, "pwd", XARGS, (CmdCallBackFunc)OsShellCmdPwd);
SHELLCMD_ENTRY(cd_shellcmd, CMD_TYPE_EX, "cd", XARGS, (CmdCallBackFunc)OsShellCmdCd);
SHELLCMD_ENTRY(cat_shellcmd, CMD_TYPE_EX, "cat", XARGS, (CmdCallBackFunc)OsShellCmdCat);
SHELLCMD_ENTRY(rm_shellcmd, CMD_TYPE_EX, "rm", XARGS, (CmdCallBackFunc)OsShellCmdRm);
SHELLCMD_ENTRY(rmdir_shellcmd, CMD_TYPE_EX, "rmdir", XARGS, (CmdCallBackFunc)OsShellCmdRmdir);
SHELLCMD_ENTRY(mkdir_shellcmd, CMD_TYPE_EX, "mkdir", XARGS, (CmdCallBackFunc)OsShellCmdMkdir);
SHELLCMD_ENTRY(cp_shellcmd, CMD_TYPE_EX, "cp", XARGS, (CmdCallBackFunc)OsShellCmdCp);
#endif
#if (LOSCFG_DEBUG_TOOLS == 1)
SHELLCMD_ENTRY(stack_shellcmd, CMD_TYPE_EX, "stack", 1, (CmdCallBackFunc)OsShellCmdStackDump);
SHELLCMD_ENTRY(hwi_shellcmd, CMD_TYPE_EX, "hwi", 1, (CmdCallBackFunc)OsShellCmdHwiDump);
SHELLCMD_ENTRY(st_shellcmd, CMD_TYPE_EX, "st", 1, (CmdCallBackFunc)OsShellCmdSchedTrace);
#endif
#if (LOSCFG_KERNEL_TRACE == 1)
SHELLCMD_ENTRY(trace_start_shellcmd, CMD_TYPE_EX, "trace_start", 0, (CmdCallBackFunc)LOS_TraceStart);
SHELLCMD_ENTRY(trace_stop_shellcmd, CMD_TYPE_EX, "trace_stop",  0, (CmdCallBackFunc)LOS_TraceStop);
SHELLCMD_ENTRY(trace_mask_shellcmd, CMD_TYPE_EX, "trace_mask",  1, (CmdCallBackFunc)OsShellCmdTraceSetMask);
SHELLCMD_ENTRY(trace_reset_shellcmd, CMD_TYPE_EX, "trace_reset", 0, (CmdCallBackFunc)LOS_TraceReset);
SHELLCMD_ENTRY(trace_dump_shellcmd, CMD_TYPE_EX, "trace_dump", 1, (CmdCallBackFunc)OsShellCmdTraceDump);
#endif
#ifdef LOSCFG_SHELL_LK
SHELLCMD_ENTRY(log_shellcmd, CMD_TYPE_EX, "log", 1, (CmdCallBackFunc)CmdLog);
#endif
#ifdef LOSCFG_SHELL_DMESG
SHELLCMD_ENTRY(dmesg_shellcmd, CMD_TYPE_STD, "dmesg", XARGS, (CmdCallBackFunc)OsShellCmdDmesg);
#endif

CmdModInfo *OsCmdInfoGet(VOID)
{
    return &cmdInfo;
}

STATIC VOID OsFreeCmdPara(CmdParsed *cmdParsed)
{
    UINT32 i;
    for (i = 0; i < cmdParsed->paramCnt; i++) {
        if (cmdParsed->paramArray[i] != NULL) {
            (VOID)LOS_MemFree(m_aucSysMem0, cmdParsed->paramArray[i]);
            cmdParsed->paramArray[i] = NULL;
        }
    }
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsCmdKeyShift(const CHAR *cmdKey, CHAR *cmdOut, UINT32 size)
{
    CHAR *output = NULL;
    CHAR *outputBak = NULL;
    UINT32 len;
    INT32 ret;
    BOOL quotes = FALSE;

    if ((cmdKey == NULL) || (cmdOut == NULL)) {
        return (UINT32)OS_ERROR;
    }

    len = strlen(cmdKey);
    if (len >= size) {
        return (UINT32)OS_ERROR;
    }
    output = (CHAR*)LOS_MemAlloc(m_aucSysMem0, len + 1);
    if (output == NULL) {
        PRINTK("malloc failure in %s[%d]", __FUNCTION__, __LINE__);
        return (UINT32)OS_ERROR;
    }
    (VOID)memset_s(output, len + 1, 0, len + 1);
    /* Backup the 'output' start address */
    outputBak = output;
    /* Scan each charactor in 'cmdKey',and squeeze the overmuch space and ignore invalid charactor */
    for (; *cmdKey != '\0'; cmdKey++) {
        if (*(cmdKey) == '\"') {
            SWITCH_QUOTES_STATUS(quotes);
        }
        if ((*cmdKey == ' ') && ((*(cmdKey + 1) == ' ') || (*(cmdKey + 1) == '\0')) && QUOTES_STATUS_CLOSE(quotes)) {
            continue;
        }
        if (*cmdKey == '\'') {
            continue;
        }
        *output = *cmdKey;
        output++;
    }
    *output = '\0';
    output = outputBak;
    len = strlen(output);
    if (*outputBak == ' ') {
        output++;
        len--;
    }
    ret = strncpy_s(cmdOut, size, output, len);
    if (ret != EOK) {
        PRINT_ERR("%s,%d strncpy_s failed, err:%d!\n", __FUNCTION__, __LINE__, ret);
        (VOID)LOS_MemFree(m_aucSysMem0, output);
        return OS_ERROR;
    }
    cmdOut[len] = '\0';

    (VOID)LOS_MemFree(m_aucSysMem0, output);

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR BOOL OsCmdKeyCheck(const CHAR *cmdKey)
{
    const CHAR *temp = cmdKey;
    enum Stat {
        STAT_NONE,
        STAT_DIGIT,
        STAT_OTHER
    } state = STAT_NONE;

    if (strlen(cmdKey) >= CMD_KEY_LEN) {
        return FALSE;
    }

    while (*temp != '\0') {
        if (!((*temp <= '9') && (*temp >= '0')) &&
            !((*temp <= 'z') && (*temp >= 'a')) &&
            !((*temp <= 'Z') && (*temp >= 'A')) &&
            (*temp != '_') && (*temp != '-')) {
            return FALSE;
        }

        if ((*temp >= '0') && (*temp <= '9')) {
            if (state == STAT_NONE) {
                state = STAT_DIGIT;
            }
        } else {
            state = STAT_OTHER;
        }

        temp++;
    }

    if (state == STAT_DIGIT) {
        return FALSE;
    }

    return TRUE;
}

LITE_OS_SEC_TEXT_MINOR VOID OsCmdAscendingInsert(CmdItemNode *cmd)
{
    CmdItemNode *cmdItem = NULL;
    CmdItemNode *cmdNext = NULL;

    if (cmd == NULL) {
        return;
    }

    for (cmdItem = LOS_DL_LIST_ENTRY((&cmdInfo.cmdList.list)->pstPrev, CmdItemNode, list);
        &cmdItem->list != &(cmdInfo.cmdList.list);) {
        cmdNext = LOS_DL_LIST_ENTRY(cmdItem->list.pstPrev, CmdItemNode, list);
        if (&cmdNext->list != &(cmdInfo.cmdList.list)) {
            if ((strncmp(cmdItem->cmd->cmdKey, cmd->cmd->cmdKey, strlen(cmd->cmd->cmdKey)) >= 0) &&
                (strncmp(cmdNext->cmd->cmdKey, cmd->cmd->cmdKey, strlen(cmd->cmd->cmdKey)) < 0)) {
                LOS_ListTailInsert(&(cmdItem->list), &(cmd->list));
                return;
            }
            cmdItem = cmdNext;
        } else {
            if (strncmp(cmd->cmd->cmdKey, cmdItem->cmd->cmdKey, strlen(cmd->cmd->cmdKey)) > 0) {
                cmdItem = cmdNext;
            }
            break;
        }
    }

    LOS_ListTailInsert(&(cmdItem->list), &(cmd->list));
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsShellKeyInit(ShellCB *shellCB)
{
    CmdKeyLink *cmdKeyLink = NULL;
    CmdKeyLink *cmdHistoryLink = NULL;

    if (shellCB == NULL) {
        return OS_ERROR;
    }
    cmdKeyLink = (CmdKeyLink *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdKeyLink));
    if (cmdKeyLink == NULL) {
        PRINT_ERR("Shell CmdKeyLink memory alloc error!\n");
        return OS_ERROR;
    }
    cmdHistoryLink = (CmdKeyLink *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdKeyLink));
    if (cmdHistoryLink == NULL) {
        (VOID)LOS_MemFree(m_aucSysMem0, cmdKeyLink);
        PRINT_ERR("Shell CmdHistoryLink memory alloc error!\n");
        return OS_ERROR;
    }

    cmdKeyLink->count = 0;
    LOS_ListInit(&(cmdKeyLink->list));
    shellCB->cmdKeyLink = (VOID *)cmdKeyLink;

    cmdHistoryLink->count = 0;
    LOS_ListInit(&(cmdHistoryLink->list));
    shellCB->cmdHistoryKeyLink = (VOID *)cmdHistoryLink;
    shellCB->cmdMaskKeyLink = (VOID *)cmdHistoryLink;
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR VOID OsShellKeyDeInit(CmdKeyLink *cmdKeyLink)
{
    CmdKeyLink *cmdtmp = NULL;
    if (cmdKeyLink == NULL) {
        return;
    }

    while (!LOS_ListEmpty(&(cmdKeyLink->list))) {
        cmdtmp = LOS_DL_LIST_ENTRY(cmdKeyLink->list.pstNext, CmdKeyLink, list);
        LOS_ListDelete(&cmdtmp->list);
        (VOID)LOS_MemFree(m_aucSysMem0, cmdtmp);
    }

    cmdKeyLink->count = 0;
    (VOID)LOS_MemFree(m_aucSysMem0, cmdKeyLink);
}

LITE_OS_SEC_TEXT_MINOR VOID OsShellKeyDeInitShellCB(const ShellCB *shellCB)
{
    if (shellCB == NULL) {
        return;
    }
    OsShellKeyDeInit(shellCB->cmdKeyLink);
    OsShellKeyDeInit(shellCB->cmdHistoryKeyLink);
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsShellSysCmdRegister(VOID)
{
    UINT32 i;
    UINT32 count = ((UINTPTR)(&g_shellcmdEnd) - (UINTPTR)(&g_shellcmdBegin)) / (UINT32)sizeof(CmdItem);
    CmdItemNode *cmdItem = NULL;

    g_cmdItemGroup = (UINT8 *)LOS_MemAlloc(m_aucSysMem0, count * sizeof(CmdItemNode));
    if (g_cmdItemGroup == NULL) {
        PRINT_ERR("[%s]System memory allocation failure!\n", __FUNCTION__);
        return (UINT32)OS_ERROR;
    }

    for (i = 0; i < count; ++i) {
        cmdItem = (CmdItemNode *)(g_cmdItemGroup + i * sizeof(CmdItemNode));
        cmdItem->cmd = &g_shellcmdBegin + i;
        OsCmdAscendingInsert(cmdItem);
    }
    cmdInfo.listNum += count;
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR VOID OsShellSysCmdUnregister(VOID)
{
    CmdItemNode *cmdItem = NULL;

    while (!LOS_ListEmpty(&(cmdInfo.cmdList.list))) {
        cmdItem = LOS_DL_LIST_ENTRY(cmdInfo.cmdList.list.pstNext, CmdItemNode, list);
        LOS_ListDelete(&cmdItem->list);
    }
    if (g_cmdItemGroup != NULL) {
        (VOID)LOS_MemFree(m_aucSysMem0, g_cmdItemGroup);
        g_cmdItemGroup = NULL;
    }
    cmdInfo.listNum = 0;
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsCmdExec(CmdParsed *cmdParsed, CHAR *cmdStr)
{
    UINT32 ret;
    CmdCallBackFunc cmdHook = NULL;
    CmdItemNode *curCmdItem = NULL;
    CmdModInfo *cmdInfoGet = OsCmdInfoGet();
    const CHAR *cmdKey = NULL;

    if ((cmdInfoGet == NULL) || (cmdParsed == NULL)) {
        return (UINT32)OS_ERROR;
    }

    if ((cmdStr != NULL) && (strlen(cmdStr) != 0)) {
        ret = OsCmdParse(cmdStr, cmdParsed);
        if (ret != LOS_OK) {
            OsFreeCmdPara(cmdParsed);
            return ret;
        }
    }

    LOS_DL_LIST_FOR_EACH_ENTRY(curCmdItem, &cmdInfoGet->cmdList.list, CmdItemNode, list) {
        cmdKey = curCmdItem->cmd->cmdKey;
        if ((cmdParsed->cmdType == curCmdItem->cmd->cmdType) &&
            (strlen(cmdKey) == strlen(cmdParsed->cmdKeyword)) &&
            (strncmp(cmdKey, (CHAR *)(cmdParsed->cmdKeyword), strlen(cmdKey)) == 0)) {
            cmdHook = curCmdItem->cmd->cmdHook;
            break;
        }
    }

    ret = OS_ERROR;
    if (cmdHook != NULL) {
        ret = (cmdHook)(cmdParsed->paramCnt, (const CHAR **)cmdParsed->paramArray);
    }
    OsFreeCmdPara(cmdParsed);

    return (UINT32)ret;
}

ShellCB *OsGetShellCb(VOID)
{
    return g_shellCB;
}

CHAR *OsShellGetWorkingDirtectory(VOID)
{
    return OsGetShellCb()->shellWorkingDirectory;
}

LITE_OS_SEC_TEXT_MINOR UINT32 OsCmdInit(VOID)
{
    UINT32 ret;
    LOS_ListInit(&(cmdInfo.cmdList.list));
    cmdInfo.listNum = 0;
    cmdInfo.initMagicFlag = SHELL_INIT_MAGIC_FLAG;
    ret = LOS_MuxCreate(&cmdInfo.muxLock);
    if (ret != LOS_OK) {
        PRINT_ERR("Create mutex for shell cmd info failed\n");
        return OS_ERROR;
    }

    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR VOID OsCmdDeInit(VOID)
{
    cmdInfo.initMagicFlag = 0;
    (VOID)LOS_MuxDelete(cmdInfo.muxLock);
}

STATIC UINT32 OsCmdItemCreate(CmdType cmdType, const CHAR *cmdKey, UINT32 paraNum, CmdCallBackFunc cmdProc)
{
    CmdItem *cmdItem = NULL;
    CmdItemNode *cmdItemNode = NULL;

    cmdItem = (CmdItem *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdItem));
    if (cmdItem == NULL) {
        return OS_ERRNO_SHELL_CMDREG_MEMALLOC_ERROR;
    }
    (VOID)memset_s(cmdItem, sizeof(CmdItem), '\0', sizeof(CmdItem));

    cmdItemNode = (CmdItemNode *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdItemNode));
    if (cmdItemNode == NULL) {
        (VOID)LOS_MemFree(m_aucSysMem0, cmdItem);
        return OS_ERRNO_SHELL_CMDREG_MEMALLOC_ERROR;
    }
    (VOID)memset_s(cmdItemNode, sizeof(CmdItemNode), '\0', sizeof(CmdItemNode));
    cmdItemNode->cmd = cmdItem;
    cmdItemNode->cmd->cmdHook = cmdProc;
    cmdItemNode->cmd->paraNum = paraNum;
    cmdItemNode->cmd->cmdType = cmdType;
    cmdItemNode->cmd->cmdKey = cmdKey;

    (VOID)LOS_MuxPend(cmdInfo.muxLock, LOS_WAIT_FOREVER);
    OsCmdAscendingInsert(cmdItemNode);
    cmdInfo.listNum++;
    (VOID)LOS_MuxPost(cmdInfo.muxLock);

    return LOS_OK;
}

/* open API */
LITE_OS_SEC_TEXT_MINOR UINT32 osCmdReg(CmdType cmdType, const CHAR *cmdKey, UINT32 paraNum, CmdCallBackFunc cmdProc)
{
    CmdItemNode *cmdItemNode = NULL;

    (VOID)LOS_MuxPend(cmdInfo.muxLock, LOS_WAIT_FOREVER);
    if (cmdInfo.initMagicFlag != SHELL_INIT_MAGIC_FLAG) {
        (VOID)LOS_MuxPost(cmdInfo.muxLock);
        PRINT_ERR("[%s] shell is not yet initialized!\n", __FUNCTION__);
        return OS_ERRNO_SHELL_NOT_INIT;
    }
    (VOID)LOS_MuxPost(cmdInfo.muxLock);

    if ((cmdProc == NULL) || (cmdKey == NULL) ||
        (cmdType >= CMD_TYPE_BUTT) || (strlen(cmdKey) >= CMD_KEY_LEN) || !strlen(cmdKey)) {
        return OS_ERRNO_SHELL_CMDREG_PARA_ERROR;
    }

    if (paraNum > CMD_MAX_PARAS) {
        if (paraNum != XARGS) {
            return OS_ERRNO_SHELL_CMDREG_PARA_ERROR;
        }
    }

    if (OsCmdKeyCheck(cmdKey) != TRUE) {
        return OS_ERRNO_SHELL_CMDREG_CMD_ERROR;
    }

    (VOID)LOS_MuxPend(cmdInfo.muxLock, LOS_WAIT_FOREVER);
    LOS_DL_LIST_FOR_EACH_ENTRY(cmdItemNode, &(cmdInfo.cmdList.list), CmdItemNode, list) {
        if ((cmdType == cmdItemNode->cmd->cmdType) &&
            ((strlen(cmdKey) == strlen(cmdItemNode->cmd->cmdKey)) &&
            (strncmp((CHAR *)(cmdItemNode->cmd->cmdKey), cmdKey, strlen(cmdKey)) == 0))) {
            (VOID)LOS_MuxPost(cmdInfo.muxLock);
            return OS_ERRNO_SHELL_CMDREG_CMD_EXIST;
        }
    }
    (VOID)LOS_MuxPost(cmdInfo.muxLock);

    return OsCmdItemCreate(cmdType, cmdKey, paraNum, cmdProc);
}

#ifdef LOSCFG_CMD_UNLOAD
LITE_OS_SEC_TEXT_MINOR UINT32 OsShellCmdDelete(CHAR *cmdKey)
{
    CmdItemNode *cmdItemNode = NULL;

    if ((cmdKey == NULL) || (strlen(cmdKey) == 0)) {
        PRINT_ERR("cmdKey is NULL or its length is zero.\n");
        return OS_ERRNO_SHELL_CMDREG_PARA_ERROR;
    }

    (VOID)LOS_MuxPend(cmdInfo.muxLock, LOS_WAIT_FOREVER);
    LOS_DL_LIST_FOR_EACH_ENTRY(cmdItemNode, &(cmdInfo.cmdList.list), CmdItemNode, list) {
        if ((strlen(cmdKey) != strlen(cmdItemNode->cmd->cmdKey)) ||
            (strncmp(cmdItemNode->cmd->cmdKey, cmdKey, strlen(cmdKey)) != 0)) {
            continue;
        }

        LOS_ListDelete(&cmdItemNode->list);
        cmdInfo.listNum--;

        /* Commands from the static SHELLCMD_ENTRY table live in the linked
         * image; only runtime-registered items own heap memory. */
        if (((UINTPTR)(cmdItemNode->cmd) < (UINTPTR)(&g_shellcmdBegin)) ||
            ((UINTPTR)(cmdItemNode->cmd) > (UINTPTR)(&g_shellcmdEnd))) {
            (VOID)LOS_MemFree(m_aucSysMem0, cmdItemNode->cmd);
            (VOID)LOS_MemFree(m_aucSysMem0, cmdItemNode);
        }

        (VOID)LOS_MuxPost(cmdInfo.muxLock);
        return LOS_OK;
    }

    (VOID)LOS_MuxPost(cmdInfo.muxLock);
    return OS_ERRNO_SHELL_CMDREG_CMD_NOT_EXIST;
}
#endif /* LOSCFG_CMD_UNLOAD */

/* ---- Tab completion and history functions (ported from hs-fbb, minus box) ---- */

STATIC VOID OsCompleteStr(const CHAR *result, const CHAR *target, CHAR *cmdKey, UINT32 *len)
{
    size_t size = strlen(result) - strlen(target);
    CHAR *des = cmdKey + *len;
    const CHAR *src = result + strlen(target);

    while (size-- > 0) {
        PRINTK("%c", *src);
        if (*len == (SHOW_MAX_LEN - 1)) {
            *des = '\0';
            break;
        }
        *des++ = *src++;
        (*len)++;
    }
}

STATIC INT32 OsTabMatchCmd(CHAR *cmdKey, UINT32 *len)
{
    INT32 count = 0;
    INT32 ret;
    CmdItemNode *cmdItemGuard = NULL;
    CmdItemNode *curCmdItem = NULL;
    CHAR *cmdMajor = cmdKey;
    CmdModInfo *cmdInfoGet = OsCmdInfoGet();

    while (*cmdMajor == SPACE) {
        cmdMajor++;
    }

    if ((cmdInfoGet == NULL) || LOS_ListEmpty(&cmdInfoGet->cmdList.list)) {
        return (INT32)OS_ERROR;
    }

    LOS_DL_LIST_FOR_EACH_ENTRY(curCmdItem, &cmdInfoGet->cmdList.list, CmdItemNode, list) {
        ret = strncmp(cmdMajor, curCmdItem->cmd->cmdKey, strlen(cmdMajor));
        if (ret > 0) {
            continue;
        } else if (ret != 0) {
            break;
        }

        if (count == 0) {
            cmdItemGuard = curCmdItem;
        }
        ++count;
    }

    if (cmdItemGuard == NULL) {
        return 0;
    }

    if (count == 1) {
        OsCompleteStr(cmdItemGuard->cmd->cmdKey, cmdMajor, cmdKey, len);
    }

    ret = count;
    if (count > 1) {
        PRINTK("\n");
        while (count--) {
            PRINTK("%s  ", cmdItemGuard->cmd->cmdKey);
            cmdItemGuard = LOS_DL_LIST_ENTRY(cmdItemGuard->list.pstNext, CmdItemNode, list);
        }
        PRINTK("\n");
    }

    return ret;
}

#ifdef LOSCFG_FS_VFS
STATIC INT32 OsStrSeparateTabStrGet(CHAR **tabStr, CmdParsed *parsed, UINT32 tabStrLen)
{
    INT32 ret = (INT32)OS_ERROR;
    CHAR *shiftStr = NULL;
    CHAR *tempStr = (CHAR *)LOS_MemAlloc(m_aucSysMem0, SHOW_MAX_LEN << 1);
    if (tempStr == NULL) {
        return (INT32)OS_ERROR;
    }

    (VOID)memset_s(tempStr, SHOW_MAX_LEN << 1, 0, SHOW_MAX_LEN << 1);
    shiftStr = tempStr + SHOW_MAX_LEN;

    if (strncpy_s(tempStr, SHOW_MAX_LEN, *tabStr, tabStrLen) != EOK) {
        goto EXIT;
    }

    parsed->cmdType = CMD_TYPE_STD;

    if (OsCmdKeyShift(tempStr, shiftStr, SHOW_MAX_LEN)) {
        goto EXIT;
    }

    if ((strlen(shiftStr) == 0) || (tempStr[strlen(tempStr) - 1] != shiftStr[strlen(shiftStr) - 1])) {
        *tabStr = "";
    } else {
        if (OsCmdTokenSplit(shiftStr, ' ', parsed)) {
            goto EXIT;
        }
        *tabStr = parsed->paramArray[parsed->paramCnt - 1];
    }
    ret = LOS_OK;

EXIT:
    (VOID)LOS_MemFree(m_aucSysMem0, tempStr);
    return ret;
}

STATIC INT32 OsStrSeparate(CHAR *tabStr, CHAR *strPath, CHAR *nameLooking, UINT32 tabStrLen)
{
    CHAR *strEnd = NULL;
    CHAR *cutPos = NULL;
    CmdParsed parsed = {0};
    CHAR *shellWorkingDirectory = OsShellGetWorkingDirtectory();
    INT32 ret;

    if (shellWorkingDirectory == NULL) {
        return (INT32)OS_ERROR;
    }
    ret = OsStrSeparateTabStrGet(&tabStr, &parsed, tabStrLen);
    if (ret != LOS_OK) {
        return ret;
    }

    if (*tabStr != '/') {
        if (strncpy_s(strPath, CMD_MAX_PATH, shellWorkingDirectory, CMD_MAX_PATH - 1) != EOK) {
            ret = (INT32)OS_ERROR;
            goto EXIT;
        }
        if (strcmp(shellWorkingDirectory, "/")) {
            if (strncat_s(strPath, CMD_MAX_PATH, "/", CMD_MAX_PATH - strlen(strPath) - 1) != EOK) {
                ret = (INT32)OS_ERROR;
                goto EXIT;
            }
        }
    }

    if (strncat_s(strPath, CMD_MAX_PATH, tabStr, CMD_MAX_PATH - strlen(strPath) - 1) != EOK) {
        ret = (INT32)OS_ERROR;
        goto EXIT;
    }

    strEnd = strrchr(strPath, '/');
    if (strEnd != NULL) {
        if (strncpy_s(nameLooking, CMD_MAX_PATH, strEnd + 1, CMD_MAX_PATH - 1) != EOK) {
            ret = (INT32)OS_ERROR;
            goto EXIT;
        }
    }

    cutPos = strrchr(strPath, '/');
    if (cutPos != NULL) {
        *(cutPos + 1) = '\0';
    }

EXIT:
    OsFreeCmdPara(&parsed);
    return ret;
}

#if (LOSCFG_USE_SHELL == 1)
STATIC INT32 OsShowPageInputControl(VOID)
{
    CHAR readChar;

    while (1) {
        readChar = (CHAR)UartGetc();
        if ((readChar == 'q') || (readChar == 'Q') || (readChar == CTRL_C)) {
            PRINTK("\n");
            return NO;
        } else if (readChar == '\r') {
            PRINTK("\b \b\b \b\b \b\b \b\b \b\b \b\b \b\b \b");
            return YES;
        }
    }
}
#else
STATIC INT32 OsShowPageInputControl(VOID)
{
    return YES;
}
#endif

STATIC INT32 OsShowPageControl(UINT32 timesPrint, UINT32 lineCap, UINT32 count)
{
    if (NEED_NEW_LINE(timesPrint, lineCap)) {
        PRINTK("\n");
        if (SCREEN_IS_FULL(timesPrint, lineCap) && (timesPrint < count)) {
            PRINTK("--More--");
            return OsShowPageInputControl();
        }
    }
    return YES;
}

#if (LOSCFG_USE_SHELL == 1)
STATIC INT32 OsSurePrintAll(UINT32 count)
{
    CHAR readChar = 0;
    PRINTK("\nDisplay all %u possibilities?(y/n)", count);
    while (1) {
        readChar = (CHAR)UartGetc();
        if ((readChar == 'n') || (readChar == 'N') || (readChar == CTRL_C)) {
            PRINTK("\n");
            return NO;
        } else if ((readChar == 'y') || (readChar == 'Y') || (readChar == '\r')) {
            return YES;
        }
    }
}
#else
STATIC INT32 OsSurePrintAll(UINT32 count)
{
    (VOID)count;
    return YES;
}
#endif

STATIC INT32 OsPrintMatchList(UINT32 count, const CHAR *strPath, const CHAR *nameLooking, UINT32 printLen)
{
    UINT32 timesPrint = 0;
    UINT32 lineCap;
    INT32 ret;
    DIR *openDir = NULL;
    struct dirent *readDir = NULL;
    CHAR formatChar[10] = {0};
    UINT32 pLen;

    pLen = (printLen > (DEFAULT_SCREEN_WIDTH - 2)) ? (DEFAULT_SCREEN_WIDTH - 2) : printLen;
    lineCap = DEFAULT_SCREEN_WIDTH / (pLen + 2);
    if (snprintf_s(formatChar, sizeof(formatChar), 7, "%%-%us  ", pLen) < 0) {
        return (INT32)OS_ERROR;
    }

    if (count > (lineCap * DEFAULT_SCREEN_HEIGNT)) {
        ret = OsSurePrintAll(count);
        if (ret != YES) {
            return ret;
        }
    }
    openDir = opendir(strPath);
    if (openDir == NULL) {
        return (INT32)OS_ERROR;
    }

    PRINTK("\n");
    for (readDir = readdir(openDir); readDir != NULL; readDir = readdir(openDir)) {
        if (strncmp(nameLooking, readDir->d_name, strlen(nameLooking)) != 0) {
            continue;
        }
        PRINTK(formatChar, readDir->d_name);
        timesPrint++;
        ret = OsShowPageControl(timesPrint, lineCap, count);
        if (ret != YES) {
            if (closedir(openDir) < 0) {
                return (INT32)OS_ERROR;
            }
            return ret;
        }
    }

    PRINTK("\n");
    if (closedir(openDir) < 0) {
        return (INT32)OS_ERROR;
    }

    return LOS_OK;
}

STATIC VOID StrncmpCut(const CHAR *s1, CHAR *s2, size_t n)
{
    size_t num = n;

    if ((n == 0) || (s1 == NULL) || (s2 == NULL)) {
        return;
    }
    do {
        if (*s1 && *s2 && (*s1 == *s2)) {
            s1++;
            s2++;
        } else {
            break;
        }
    } while (--num != 0);
    if (n > 0) {
        while (num-- != 0) {
            *s2++ = '\0';
        }
    }
    return;
}

STATIC INT32 OsExecNameMatch(const CHAR *strPath, const CHAR *nameLooking, CHAR *strObj, UINT32 *maxLen)
{
    INT32 count = 0;
    DIR *openDir = NULL;
    struct dirent *readDir = NULL;

    openDir = opendir(strPath);
    if (openDir == NULL) {
        return (INT32)OS_ERROR;
    }

    for (readDir = readdir(openDir); readDir != NULL; readDir = readdir(openDir)) {
        if (strncmp(nameLooking, readDir->d_name, strlen(nameLooking)) != 0) {
            continue;
        }
        if (count == 0) {
            if (strncpy_s(strObj, CMD_MAX_PATH, readDir->d_name, CMD_MAX_PATH - 1) != EOK) {
                (VOID)closedir(openDir);
                return (INT32)OS_ERROR;
            }
            *maxLen = (UINT32)strlen(readDir->d_name);
        } else {
            StrncmpCut(readDir->d_name, strObj, strlen(strObj));
            if (strlen(readDir->d_name) > *maxLen) {
                *maxLen = (UINT32)strlen(readDir->d_name);
            }
        }
        count++;
    }

    if (closedir(openDir) < 0) {
        return (INT32)OS_ERROR;
    }

    return count;
}

STATIC INT32 OsTabMatchFile(CHAR *cmdKey, UINT32 *len)
{
    UINT32 maxLen = 0;
    INT32 count;
    CHAR *strOutput = NULL;
    CHAR *strCmp = NULL;
    CHAR *dirOpen = (CHAR *)LOS_MemAlloc(m_aucSysMem0, CMD_MAX_PATH * 3); /* 3:dirOpen\strOutput\strCmp */
    if (dirOpen == NULL) {
        return (INT32)OS_ERROR;
    }

    (VOID)memset_s(dirOpen, CMD_MAX_PATH * 3, 0, CMD_MAX_PATH * 3);
    strOutput = dirOpen + CMD_MAX_PATH;
    strCmp = strOutput + CMD_MAX_PATH;

    if (OsStrSeparate(cmdKey, dirOpen, strCmp, *len)) {
        (VOID)LOS_MemFree(m_aucSysMem0, dirOpen);
        return (INT32)OS_ERROR;
    }

    count = OsExecNameMatch(dirOpen, strCmp, strOutput, &maxLen);
    if (count >= 1) {
        OsCompleteStr(strOutput, strCmp, cmdKey, len);

        if (count == 1) {
            (VOID)LOS_MemFree(m_aucSysMem0, dirOpen);
            return 1;
        }
        if (OsPrintMatchList((UINT32)count, dirOpen, strCmp, maxLen) == -1) {
            (VOID)LOS_MemFree(m_aucSysMem0, dirOpen);
            return (INT32)OS_ERROR;
        }
    }

    (VOID)LOS_MemFree(m_aucSysMem0, dirOpen);
    return count;
}
#else
STATIC INT32 OsTabMatchFile(CHAR *cmdKey, UINT32 *len)
{
    (VOID)cmdKey;
    (VOID)len;
    return 0;
}
#endif /* LOSCFG_FS_VFS */

LITE_OS_SEC_TEXT_MINOR INT32 OsTabCompletion(CHAR *cmdKey, UINT32 *len)
{
    INT32 count = 0;
    CHAR *space = NULL;
    CHAR *cmdMainStr = cmdKey;

    if ((cmdKey == NULL) || (len == NULL)) {
        return (INT32)OS_ERROR;
    }

    while (*cmdMainStr == SPACE) {
        cmdMainStr++;
    }

    space = strrchr(cmdMainStr, SPACE);
    if ((space == NULL) && (*cmdMainStr != '\0')) {
        count = OsTabMatchCmd(cmdKey, len);
    }

    if (count == 0) {
        count = OsTabMatchFile(cmdKey, len);
    }

    return count;
}

LITE_OS_SEC_TEXT_MINOR VOID OsShellCmdPush(const CHAR *string, CmdKeyLink *cmdKeyLink)
{
    CmdKeyLink *cmdNewNode = NULL;
    UINT32 len;

    if ((string == NULL) || (strlen(string) == 0)) {
        return;
    }

    len = (UINT32)strlen(string);
    cmdNewNode = (CmdKeyLink *)LOS_MemAlloc(m_aucSysMem0, sizeof(CmdKeyLink) + len + 1);
    if (cmdNewNode == NULL) {
        return;
    }

    (VOID)memset_s(cmdNewNode, sizeof(CmdKeyLink) + len + 1, 0, sizeof(CmdKeyLink) + len + 1);
    if (strncpy_s(cmdNewNode->cmdString, len + 1, string, len) != EOK) {
        (VOID)LOS_MemFree(m_aucSysMem0, cmdNewNode);
        return;
    }

    LOS_ListTailInsert(&cmdKeyLink->list, &cmdNewNode->list);

    return;
}

LITE_OS_SEC_TEXT_MINOR VOID OsShellHistoryShow(UINT32 value, ShellCB *shellCB)
{
    CmdKeyLink *cmd = NULL;
    CmdKeyLink *cmdNode = shellCB->cmdHistoryKeyLink;
    CmdKeyLink *cmdMask = shellCB->cmdMaskKeyLink;
    errno_t ret;

    HISTORY_LOCK(shellCB);
    if (value == CMD_KEY_DOWN) {
        if (cmdMask == cmdNode) {
            goto END;
        }
        cmd = LOS_DL_LIST_ENTRY(cmdMask->list.pstNext, CmdKeyLink, list);
    } else if (value == CMD_KEY_UP) {
        cmd = LOS_DL_LIST_ENTRY(cmdMask->list.pstPrev, CmdKeyLink, list);
    }

    if (cmd != cmdNode) {
        cmdMask = cmd;
    } else {
        goto END;
    }

    while (shellCB->shellBufOffset--) {
        PRINTK("\b \b");
    }
    PRINTK("%s", cmdMask->cmdString);
    shellCB->shellBufOffset = (UINT32)strlen(cmdMask->cmdString);
    (VOID)memset_s(shellCB->shellBuf, SHOW_MAX_LEN, 0, SHOW_MAX_LEN);
    ret = memcpy_s(shellCB->shellBuf, SHOW_MAX_LEN, cmdMask->cmdString, shellCB->shellBufOffset);
    if (ret != EOK) {
        PRINT_ERR("%s, %d memcpy failed!\n", __FUNCTION__, __LINE__);
        goto END;
    }
    shellCB->cmdMaskKeyLink = (VOID *)cmdMask;

END:
    HISTORY_UNLOCK(shellCB);
    return;
}

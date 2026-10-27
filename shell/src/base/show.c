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

#include "show.h"
#include "shmsg.h"
#include "shell_pri.h"
#include "los_memory.h"
#include "los_mux.h"
#include "securec.h"

STATIC BOOL g_shellSourceFlag = FALSE;

STATIC BOOL *OsShellSourceFlagGet(VOID)
{
    return &g_shellSourceFlag;
}

STATIC UINT32 OsShellSourceInit(VOID)
{
    UINT32 ret;
    BOOL *flag = OsShellSourceFlagGet();
    if (flag == NULL) {
        return LOS_NOK;
    }

    if (*flag == TRUE) {
        return LOS_OK;
    }

    ret = OsCmdInit();
    if (ret != LOS_OK) {
        return ret;
    }

    ret = OsShellSysCmdRegister();
    if (ret != LOS_OK) {
        OsCmdDeInit();
        return ret;
    }

    *flag = TRUE;
    return LOS_OK;
}

STATIC VOID OsShellSourceDeInit(VOID)
{
    BOOL *flag = OsShellSourceFlagGet();
    if (flag == NULL) {
        return;
    }

    OsCmdDeInit();
    OsShellSysCmdUnregister();
    *flag = FALSE;
}

STATIC UINT32 OsShellCreateTask(ShellCB *shellCB)
{
    UINT32 ret = ShellTaskInit(shellCB);
    if (ret != LOS_OK) {
        return ret;
    }

    ret = ShellEntryInit(shellCB);
    if (ret != LOS_OK) {
        (VOID)LOS_TaskDelete(shellCB->shellTaskHandle);
    }
    return ret;
}

STATIC UINT32 OsShellCBInit(ShellCB **shellCB)
{
    UINT32 ret = LOS_NOK;
    ShellCB *cb = NULL;

    if (OsShellSourceInit() != LOS_OK) {
        PRINT_ERR("shell source init error.\n");
        return LOS_NOK;
    }

    cb = LOS_MemAlloc(m_aucSysMem0, sizeof(ShellCB));
    if (cb == NULL) {
        PRINT_ERR("memory alloc error.\n");
        goto ERR_OUT_SOURCE_DEINIT;
    }
    (VOID)memset_s(cb, sizeof(ShellCB), 0, sizeof(ShellCB));

    *shellCB = cb;
    return LOS_OK;

ERR_OUT_SOURCE_DEINIT:
    OsShellSourceDeInit();
    return ret;
}

STATIC VOID OsShellCBDeinit(ShellCB * const *shellCB)
{
    (VOID)LOS_MemFree((VOID *)m_aucSysMem0, *shellCB);
    g_shellCB = NULL;
    OsShellSourceDeInit();
}

UINT32 OsShellInit(VOID)
{
    ShellCB *shellCB = NULL;

    if (g_shellCB != NULL) {
        return LOS_OK;
    }

    if (OsShellCBInit(&shellCB) != LOS_OK) {
        PRINT_ERR("shellCB init error.\n");
        return LOS_NOK;
    }
    g_shellCB = shellCB;

    if (LOS_MuxCreate(&shellCB->keyMutex) != LOS_OK) {
        PRINT_ERR("key mutex create error.\n");
        goto ERR_OUT_SHELLCB_DEINIT;
    }

    if (LOS_MuxCreate(&shellCB->historyMutex) != LOS_OK) {
        PRINT_ERR("history mutex create error.\n");
        goto ERR_OUT_DELETE_KEY_MUX;
    }

    if (strncpy_s(shellCB->shellWorkingDirectory, PATH_MAX, "/", 1) != EOK) {
        PRINT_ERR("strncpy_s error.\n");
        goto ERR_OUT_DELETE_HISTORY_MUX;
    }

    if (OsShellKeyInit(shellCB) != LOS_OK) {
        PRINT_ERR("shell key init error.\n");
        goto ERR_OUT_DELETE_HISTORY_MUX;
    }

    if (OsShellCreateTask(shellCB) != LOS_OK) {
        PRINT_ERR("shell task create error.\n");
        goto ERR_OUT_DEINIT_KEY;
    }

    return LOS_OK;

ERR_OUT_DEINIT_KEY:
    OsShellKeyDeInitShellCB(shellCB);
ERR_OUT_DELETE_HISTORY_MUX:
    (VOID)LOS_MuxDelete(shellCB->historyMutex);
ERR_OUT_DELETE_KEY_MUX:
    (VOID)LOS_MuxDelete(shellCB->keyMutex);
ERR_OUT_SHELLCB_DEINIT:
    OsShellCBDeinit(&shellCB);
    return LOS_NOK;
}

#if (LOSCFG_USE_SHELL == 1)
INT32 OsShellDeinit(INT32 consoleId)
{
    (VOID)consoleId;

    ShellCB *shellCB = g_shellCB;
    if (shellCB == NULL) {
        PRINT_ERR("shell deinit error.\n");
        return -1;
    }

    g_shellCB = NULL;
    ShellTaskDeinit(shellCB);

    return 0;
}
#endif

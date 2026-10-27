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

#ifndef _HWLITEOS_SHELL_PRI_H
#define _HWLITEOS_SHELL_PRI_H

#include "los_config.h"
#include "shcmd.h"
#include "shell.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define CTRL_C 0x03 /* 0x03: ctrl+c ASCII */
#define SPACE  0x20 /* 0x20: space ASCII */

#define KEY_LOCK(shellCB) do {                                                          \
    UINT32 ret_;                                                                        \
    ret_ = LOS_MuxPend((shellCB)->keyMutex, LOS_WAIT_FOREVER);                          \
    if (ret_ != LOS_OK) {                                                               \
        PRINT_ERR("%s %d, mutex lock failed : 0x%x\n", __FUNCTION__, __LINE__, ret_);   \
    }                                                                                   \
} while (0)

#define KEY_UNLOCK(shellCB) do {                                                        \
    UINT32 ret_;                                                                        \
    ret_ = LOS_MuxPost((shellCB)->keyMutex);                                            \
    if (ret_ != LOS_OK) {                                                               \
        PRINT_ERR("%s %d, mutex unlock failed : 0x%x\n", __FUNCTION__, __LINE__, ret_); \
    }                                                                                   \
} while (0)

#define HISTORY_LOCK(shellCB) do {                                                      \
    UINT32 ret_;                                                                        \
    ret_ = LOS_MuxPend((shellCB)->historyMutex, LOS_WAIT_FOREVER);                      \
    if (ret_ != LOS_OK) {                                                               \
        PRINT_ERR("%s %d, mutex lock failed : 0x%x\n", __FUNCTION__, __LINE__, ret_);   \
    }                                                                                   \
} while (0)

#define HISTORY_UNLOCK(shellCB) do {                                                    \
    UINT32 ret_;                                                                        \
    ret_ = LOS_MuxPost((shellCB)->historyMutex);                                        \
    if (ret_ != LOS_OK) {                                                               \
        PRINT_ERR("%s %d, mutex unlock failed : 0x%x\n", __FUNCTION__, __LINE__, ret_); \
    }                                                                                   \
} while (0)

#ifdef LOSCFG_SHELL_LK
extern VOID OsLkLoggerInit(const CHAR *str);
#endif

extern ShellCB *g_shellCB;

extern CmdItem g_shellcmdBegin;
extern CmdItem g_shellcmdEnd;

extern VOID *ShellTask(UINT32 param);
extern VOID *ShellEntry(UINT32 param);
extern VOID *ShellStdinLoop(ShellCB *shellCB);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _HWLITEOS_SHELL_PRI_H */

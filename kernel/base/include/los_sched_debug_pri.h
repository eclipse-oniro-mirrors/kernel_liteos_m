/* ----------------------------------------------------------------------------
 * Copyright (c) 2013-2022 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: Schedule Statistics Debug Private HeadFile
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
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
 * --------------------------------------------------------------------------- */

#ifndef _LOS_SCHED_DEBUG_PRI_H
#define _LOS_SCHED_DEBUG_PRI_H

#include "los_task_pri.h"
#include "los_interrupt.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef LOSCFG_DEBUG_SCHED_STATISTICS

typedef struct {
    UINT64      pastTime;               /* Statistics window length in ns */
    UINT32      idleTaskTimePercent;    /* Percentage of running duration of idle task, x100 */
    UINT32      taskSwiNum;             /* Context switch count */
    UINT32      hwiNum;                 /* Interrupt count, tick excluded */
    UINT32      noIdleTaskSwiAvgPri;    /* Average priority of non-idle switches, x100 */
    UINT32      hiTaskTimePercent;      /* Percentage of running duration of high-priority tasks, x100 */
    UINT32      hiTaskSwiNum;           /* High-priority task switch count */
    UINT32      hiTaskAvgRunTime;       /* Average running duration of high-priority tasks, in us */
} SchedStatCpuInfo;

extern VOID OsSchedStatHwi(HWI_HANDLE_T hwiNum);
extern VOID OsSchedStatPause(VOID);
extern VOID OsSchedStatResume(VOID);
extern UINT32 OsSchedStatCpuInfoGet(SchedStatCpuInfo *info, UINT32 cpuId);
extern VOID OsSchedStatStart(VOID);
extern VOID OsSchedStatStop(VOID);
extern VOID OsSchedStatShow(VOID);

#ifdef LOSCFG_SHELL
extern UINT32 OsShellCmdSchedStatStart(UINT32 argc, const CHAR **argv);
extern UINT32 OsShellCmdSchedStatStop(UINT32 argc, const CHAR **argv);
extern UINT32 OsShellCmdSchedStatInfo(UINT32 argc, const CHAR **argv);
#endif

#endif /* LOSCFG_DEBUG_SCHED_STATISTICS */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_SCHED_DEBUG_PRI_H */

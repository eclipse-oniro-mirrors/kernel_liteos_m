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

#include "osTest.h"
#include "los_task.h"
#include "los_mux.h"
#include "los_queue.h"
#include "los_sem.h"
#include "los_swtmr.h"
#include "los_event.h"
#include "los_interrupt.h"

VOID LosFlashLinkRef(VOID)
{
    (VOID)LOS_TaskCreate(NULL, NULL);
    (VOID)LOS_TaskCreateOnly(NULL, NULL);
    (VOID)LOS_TaskResume(OS_INVALID);
    (VOID)LOS_TaskDelete(OS_INVALID);
    (VOID)LOS_TaskDelay(0);
    (VOID)LOS_TaskResRecycle();
    (VOID)LOS_TaskYield();
    (VOID)LOS_TaskPriSet(OS_INVALID, 0);

    (VOID)LOS_MuxCreate(NULL);
    (VOID)LOS_MuxPend(0, 0);
    (VOID)LOS_MuxPost(0);

    (VOID)LOS_QueueCreate(NULL, 0, NULL, 0, 0);
    (VOID)LOS_QueueReadCopy(0, NULL, NULL, 0);
    (VOID)LOS_QueueWriteCopy(0, NULL, 0, 0);
    (VOID)LOS_QueueRead(0, NULL, 0, 0);
    (VOID)LOS_QueueWrite(0, NULL, 0, 0);

    (VOID)LOS_SemCreate(0, NULL);
    (VOID)LOS_SemDelete(0);
    (VOID)LOS_SemPend(0, 0);
    (VOID)LOS_SemPost(0);

    (VOID)LOS_SwtmrCreate(0, 0, NULL, NULL, 0);
    (VOID)LOS_SwtmrStart(0);
    (VOID)LOS_SwtmrStop(0);
    (VOID)LOS_SwtmrDelete(0);

    (VOID)LOS_EventInit(NULL);
    (VOID)LOS_EventRead(NULL, 0, 0, 0);
    (VOID)LOS_EventWrite(NULL, 0);
    (VOID)LOS_EventClear(NULL, 0);
    (VOID)LOS_EventPoll(NULL, 0, 0);
    (VOID)LOS_EventDestroy(NULL);

    (VOID)LOS_HwiCreate(0, 0, 0, NULL, NULL);
    (VOID)LOS_HwiDelete(0, NULL);
    (VOID)LOS_HwiEnable(0);
    (VOID)LOS_HwiDisable(0);
    (VOID)LOS_HwiClear(0);
    (VOID)LOS_HwiTrigger(0);
}

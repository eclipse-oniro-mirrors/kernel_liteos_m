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

/* Shared helpers for the boxA usr mux cases. See the header for the design
 * notes; this file only carries the ONE definition of each helper. */

#include "It_los_boxA_usr_mux_util.h"

#if (LOSCFG_TEST_KERNEL_MUX == 1)

UINT32 BoxMuxSpawn(TSK_ENTRY_FUNC entry, const CHAR *name, UINT32 *taskId)
{
    TSK_INIT_PARAM_S initParam = {0};

    initParam.pfnTaskEntry = entry;
    initParam.uwStackSize = BOX_MUX_TASK_STACK_SIZE;
    initParam.pcName = (CHAR *)name;
    initParam.usTaskPrio = BOX_MUX_CHILD_PRIO;
    initParam.uwResved = LOS_TASK_STATUS_DETACHED;
    return LOS_TaskCreate(taskId, &initParam);
}

BOOL BoxMuxWaitState(const volatile UINT32 *state, UINT32 expect)
{
    UINT32 spin;

    for (spin = 0; spin < BOX_MUX_WAIT_SPIN; spin++) {
        if (*state == expect) {
            return TRUE;
        }
        LOS_TaskDelay(BOX_MUX_WAIT_TICKS);
    }
    return (*state == expect);
}

VOID BoxMuxReap(VOID)
{
    /* The child returns into OsUsrTaskExit; one tick lets the kernel delete it
     * and queue its user stack, then the drain frees the stack bytes. */
    LOS_TaskDelay(BOX_MUX_REAP_TICKS);
    LOS_TaskStackRecycle();
}

#endif /* LOSCFG_TEST_KERNEL_MUX */

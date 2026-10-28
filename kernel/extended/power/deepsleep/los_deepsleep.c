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

#include "los_deepsleep_pri.h"
#include "los_runstop_pri.h"
#include "los_pm.h"
#include "los_tick.h"
#include "los_sched_pri.h"
#include "los_interrupt.h"
#include "los_hwi_pri.h"
#include "los_common_interrupt.h"
#include "los_arch_pm.h"

STATIC UINT32 OsCalExtSleepCycles(LosPmCB *pm)
{
    UINT64 currTime = OsGetCurrSchedTimeCycle();
    UINT64 expireTime = OsSchedGetNextExpireTime(currTime);
    UINT64 realSleepTime = expireTime - currTime;
    UINT64 sleepCycle = OS_SYS_CYCLE_TO_NS(realSleepTime, g_sysClock);

    return (UINT32)OS_SYS_NS_TO_CYCLE(sleepCycle, pm->tickTimer->freq);
}

STATIC VOID OsDeepSleepResume(VOID)
{
    OsResumeInterruptSystem();

    RUNOPS_CALL_FUNC_VOID(systemWakeup);
    OsSRRestoreRegister();
}

VOID OsEnterDeepSleepMainCore(LosPmCB *pm)
{
    LosPmTickTimer *tickTimer = pm->tickTimer;
    UINT32 sleepCycles;

    RUNOPS_CALL_FUNC_VOID(suspendConfig);
    g_resumeFromImg = LOS_COLD_RESET;
    OsSRSaveRegister();

    if (g_resumeFromImg == LOS_COLD_RESET) {
        g_resumeFromImg = LOS_DEEP_SLEEP_RESET;
        ISB();

        pm->enterSleepTime = LOS_SysCycleGet();
        tickTimer->tickLock();
        tickTimer->timerStart(OsCalExtSleepCycles(pm));
        RUNOPS_CALL_FUNC_VOID(enterDeepSleep);
    }
    RUNOPS_CALL_FUNC_VOID(resumeConfig);

    sleepCycles = (UINT32)tickTimer->timerCycleGet();
    tickTimer->timerStop();
    OsPmTickCompensate(pm, sleepCycles);
}

VOID OsLowpowerWakeupFromReset(VOID)
{
#ifdef LOSCFG_KERNEL_RUNSTOP
    if (g_resumeFromImg == LOS_RUN_STOP_RESET) {
        OsSystemWakeup();
        return;
    }
#endif
    if (g_resumeFromImg == LOS_DEEP_SLEEP_RESET) {
        OsDeepSleepResume();
    }
}


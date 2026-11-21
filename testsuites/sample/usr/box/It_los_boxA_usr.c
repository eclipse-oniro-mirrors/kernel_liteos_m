/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
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
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* UTS usr-world BOX suite: runs in boxA USER mode through the shared
 * iCunit copy (COMMON sections), invoked by the box test entry
 * (usr_demo/tboxA/boxA_test_main.c). Suite hook is declared in osTest.h
 * (ItSuiteLosBoxUsr). */

#include "osTest.h"
#include "It_los_boxA_usr.h"

VOID ItSuiteLosBoxUsr(VOID)
{

    /* Memory cases (mem/). Baseline first for the cleanest numbers (it does
     * not require a pristine pool - only the accounting identity is
     * asserted); the API groups follow the behaviour groups. */
    ItLosBoxUsrMem001();
    ItLosBoxUsrMem002();
    ItLosBoxUsrMem003();
    ItLosBoxUsrMem004();
    ItLosBoxUsrMem005();
    ItLosBoxUsrMem006();
    ItLosBoxUsrMem007();
    ItLosBoxUsrMem008();
    ItLosBoxUsrMemApi001();
    ItLosBoxUsrMemApi002();
    ItLosBoxUsrMemApi003();
    ItLosBoxUsrMemApi004();
    ItLosBoxUsrMemApi005();
    ItLosBoxUsrMemApi008();
    ItLosBoxUsrMemC001();
    ItLosBoxUsrMemC006();
    ItLosBoxUsrMemE001();
    ItLosBoxUsrMemF002();
    ItLosBoxUsrMemF003();
    ItLosBoxUsrMemTsk001();
    ItLosBoxUsrMemTsk002();
    ItLosBoxUsrMemTsk003();
    ItLosBoxUsrMemTsk004();
    ItLosBoxUsrMemTsk005();
    ItLosBoxUsrMemTsk006();
    ItLosBoxUsrMemTsk007();
    ItLosBoxUsrMemTsk008();
    ItLosBoxUsrMemTsk009();
    ItLosBoxUsrMemTsk010();


    /* Functional BOX usr cases, module-tagged (task/sem/condvar/queue/
     * event/swtmr subdirs): condvar / event / swtmr / queue / task /
     * semaphore lifecycle. Independent of the heap state. */
    ItLosBoxUsrTask001();
    ItLosBoxUsrSem001();
    ItLosBoxUsrCondvar001();
    ItLosBoxUsrEvent002();
    ItLosBoxUsrSwtmr002();
    ItLosBoxUsrQueue001();
    ItLosBoxUsrEvent003();

    /* MEM-API-006 runs ABSOLUTELY LAST: LOS_MemDeInit leaks the pool LOCK, and
     * under BOX that lock is a MUX drawn from the box mux pool (LIMIT 4, one of
     * them already owned by the heap lock). Keeping it after every other case -
     * including the functional usr ones - stops the leaked slot from starving
     * C001 / C006 / Usr003, which also need muxes. Kernel defect, reported
     * separately. */
    ItLosBoxUsrMemApi006();

    /* Module-subdir usr cases: tick readouts, swtmr query/bring-up,
     * SID event clear - all through the syscall face. */
    ItLosBoxUsrTick001();
    ItLosBoxUsrTick002();
    ItLosBoxUsrSwtmr001();
    ItLosBoxUsrEvent001();

}

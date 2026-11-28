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

#include "uts_phase.h"

/* Compiled ONCE in the usr_demo box_usr_test module: the definition is
 * routed into .COMMON_BSS (MPU R1, RW from both worlds) by the liteos.ld
 * file-name rules, and zeroed by the startup common_bss loop. */
volatile UINT32 g_utsPhase = UTS_PHASE_IDLE;

/* Set by the kernel coordinator before it releases boxA; see the comment on the
 * declaration in uts_phase.h for why the box cannot answer this itself. */
volatile UINT32 g_utsHasBoxB = 0;

/* How many usr suites the kernel released; written once by the kernel. */
volatile UINT32 g_utsBoxCount = 0;

/* One completion flag per world - see the comment in uts_phase.h for why this
 * is not a shared counter (no lock is available to a box). */
volatile UINT32 g_utsDoneByWorld[UTS_DONE_WORLD_NUM] = {0};

/* Sequential-mode baton (see UTS_CONCURRENT_MODE in uts_phase.h).
 * Only boxA writes it; only boxB reads it. */
volatile UINT32 g_utsTurnB = 0;
#ifdef LOSCFG_TEST_BOX_FAULT
volatile UINT32 g_utsProbeState;
volatile UINT32 g_utsProbeDestroyed;
volatile UINT32 g_utsProbeScratch;
#endif

/* Coordinator helper: TRUE when every released suite has set its flag.
 * Reads only; the kernel is the single reader of these slots. */
UINT32 ICunitUtsAllDone(UINT32 boxCount)
{
    UINT32 world;

    /* World 0 is the kernel and is never expected here, so a release of N
     * suites covers worlds 1..N. */
    for (world = 1; (world <= boxCount) && (world < UTS_DONE_WORLD_NUM); world++) {
        if (g_utsDoneByWorld[world] == 0U) {
            return 0U;
        }
    }
    return 1U;
}

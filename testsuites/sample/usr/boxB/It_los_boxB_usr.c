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


/* ============================================================================
 * It_los_boxB_usr.c - boxB suite entry point.
 *
 * Runs in boxB USER mode, invoked by the boxB test entry
 * (usr_demo/tboxB/boxB_test_main.c). Registered through the shared iCunit copy
 * so boxB's cases land in the SAME report as the kernel phase and boxA.
 *
 * ---------------------------------------------------------------------------
 * ORDER MATTERS - boxB has a hard resource ceiling
 * ---------------------------------------------------------------------------
 * LOSCFG_BOXB_MUX_LIMIT is 1 and the heap lock owns it, so boxB has ZERO spare
 * mux slots. LOS_MemDeInit leaks the pool lock (see BOX-B-003), which means a
 * DeInit/Init cycle consumes the box's last lock and cannot be repeated.
 *
 * BOX-B-003 therefore runs LAST: once it destroys the heap, no later case in
 * this suite could allocate anything, and the entry task holds the only TCB so
 * nothing can rebuild it.
 *
 *   BOX-B-001  baseline + accounting identity
 *   BOX-B-002  alloc/align/realloc/free contract
 *   BOX-B-004  the five accounting getters agree
 *   BOX-B-005  drain, fragment, recover
 *   BOX-B-003  destroy and rebuild the heap  <-- last, it may not come back
 * ========================================================================== */

#include "osTest.h"
#include "It_los_boxB_usr.h"

VOID ItSuiteLosBoxBUsr(VOID)
{
    ItLosBoxBUsr001();
    ItLosBoxBUsr002();
    ItLosBoxBUsr004();
    ItLosBoxBUsr005();

    /* LAST on purpose: this one destroys the box's heap and, because of the
     * MemDeInit lock leak, cannot rebuild it on boxB (MUX_LIMIT = 1). Anything
     * after it would find no pool at all. */
    ItLosBoxBUsr003();
}

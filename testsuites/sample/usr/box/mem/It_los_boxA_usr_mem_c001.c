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
 * MEM-C-001  IPC pool exhaustion does not touch the box heap  (group C)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Mutex and semaphore control blocks live in .box.res static pools, NOT on the
 * heap - that is the whole point of the box resource-accounting design. This
 * case proves it by exhausting both pools and requiring the heap snapshot to be
 * byte-identical throughout.
 *
 * It matters because the alternative (control blocks allocated per object) would
 * let an application exhaust the heap simply by creating IPC objects, which is a
 * much easier mistake to make than allocating large buffers.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE / WHAT A FAILURE TELLS YOU
 * ---------------------------------------------------------------------------
 * The case reports how many muxes / semaphores the pool granted. Two things are
 * worth knowing when reading it:
 *
 *   - the box heap lock occupies ONE mux slot permanently (it is created when the
 *     box entry calls LOS_MemInit), so the usable mux count is one less than
 *     LOSCFG_BOXx_MUX_LIMIT. That is by design, not a leak.
 *   - a heap movement during this case means an IPC create path is allocating
 *     from the box heap, which would make an application's heap budget depend on
 *     its IPC usage.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define BOX_MEM_C001_MAX  16U

static UINT32 g_c001Mux[BOX_MEM_C001_MAX];
static UINT32 g_c001Sem[BOX_MEM_C001_MAX];

static UINT32 MemC001Case(VOID)
{
    static const CHAR *caseName = "MEM-C-001";
    BoxMemSnap s0;
    BoxMemSnap s1;
    UINT32 muxes = 0;
    UINT32 sems = 0;
    UINT32 fails = 0;
    UINT32 i;

    if (!BoxMemSnapGet(&s0, "c001 init")) {
        ICUNIT_ASSERT_EQUAL(1, 0, 0);
        return LOS_OK;
    }

    /* ---- exhaust the mux pool ---- */
    for (i = 0; i < BOX_MEM_C001_MAX; i++) {
        if (LOS_MuxCreate(&g_c001Mux[i]) != LOS_OK) {
            break;
        }
        muxes++;
    }
    /* ---- exhaust the semaphore pool ---- */
    for (i = 0; i < BOX_MEM_C001_MAX; i++) {
        if (LOS_SemCreate(0, &g_c001Sem[i]) != LOS_OK) {
            break;
        }
        sems++;
    }
    BOX_MEM_TRACE("[MEM] %s muxes=%u sems=%u\n", caseName, muxes, sems);

    /* Both pools must have granted at least one object, otherwise the case did
     * not exercise anything. */
    if (muxes == 0) {
        fails += BoxMemFail(caseName, "no mux could be created", muxes, 1U);
    }
    if (sems == 0) {
        fails += BoxMemFail(caseName, "no sem could be created", sems, 1U);
    }

    /* ---- the heap must not have moved ---- */
    if (!BoxMemSnapGet(&s1, "c001 full")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "IPC create moved the heap",
                            s1.freeSize, s0.freeSize);
    }

    /* ---- release and verify again ---- */
    for (i = 0; i < muxes; i++) {
        (VOID)LOS_MuxDelete(g_c001Mux[i]);
    }
    for (i = 0; i < sems; i++) {
        (VOID)LOS_SemDelete(g_c001Sem[i]);
    }
    if (!BoxMemSnapGet(&s1, "c001 final")) {
        fails++;
    } else if (!BoxMemSame(&s0, &s1)) {
        fails += BoxMemFail(caseName, "IPC delete moved the heap",
                            s1.freeSize, s0.freeSize);
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemC001(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemC001", MemC001Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}

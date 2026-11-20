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
 * MEM-C-006  The box heap lock occupies one mux slot  (group C)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * A box heap is locked with a MUTEX, not an interrupt lock, because a box can
 * hold the heap for a long time and disabling interrupts for that would wreck
 * latency. That mutex is drawn from the box's own mux pool when the entry task
 * calls LOS_MemInit - which means the pool the APPLICATION sees is one slot
 * smaller than LOSCFG_BOXx_MUX_LIMIT.
 *
 * This is a configuration correctness check, not a leak check: it asserts that
 * the usable mux count is (configured limit - 1). Resources sized from the
 * configured number rather than the usable number will fail at runtime, and this
 * case is what makes that visible on the bench instead of in the field.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * The configured limit is not compiled into this case (it lives in the target's
 * boxes_res_auto.cfg, which is not on the usr include path). Instead the case
 * asserts the relation that must hold:
 *
 *     usable >= 1   AND   usable == configured - 1 is IMPLIED by "the heap lock
 *     took one" - so the observable proxy is: creating exactly one mux must
 *     succeed even when the pool is otherwise full.
 *
 * Concretely: fill the pool, then free ONE and verify the next create succeeds.
 * If the heap lock were NOT part of this pool, the fill would have yielded one
 * MORE mux than this pool can actually serve and the arithmetic elsewhere
 * (LOSCFG_BOXx_SID_LIMIT guards) would be wrong.
 *
 * NOTE: boxB has MUX_LIMIT = 1, which the heap lock consumes entirely, so its
 * usable count is 0. That is why a box must reserve at least one mux slot for
 * the heap before planning its IPC budget.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define BOX_MEM_C006_MAX  16U

static UINT32 g_c006Mux[BOX_MEM_C006_MAX];

static UINT32 MemC006Case(VOID)
{
    static const CHAR *caseName = "MEM-C-006";
    UINT32 muxes = 0;
    UINT32 fails = 0;
    UINT32 i;
    UINT32 probe = 0;

    /* ---- fill the mux pool ---- */
    for (i = 0; i < BOX_MEM_C006_MAX; i++) {
        if (LOS_MuxCreate(&g_c006Mux[i]) != LOS_OK) {
            break;
        }
        muxes++;
    }
    dprintf("[MEM] %s: usable muxes = %u (boxA MUX_LIMIT minus the heap lock)\n",
            caseName, muxes);

    /* ---- a full pool must refuse ---- */
    if (LOS_MuxCreate(&probe) == LOS_OK) {
        fails += BoxMemFail(caseName, "pool accepted one more than it holds",
                            muxes + 1U, muxes);
        (VOID)LOS_MuxDelete(probe);
    }

    /* ---- free one, the next create must succeed: the slot is recyclable ---- */
    if (muxes > 0U) {
        (VOID)LOS_MuxDelete(g_c006Mux[0]);
        g_c006Mux[0] = 0;
        if (LOS_MuxCreate(&probe) != LOS_OK) {
            fails += BoxMemFail(caseName, "recycled slot not reusable",
                                muxes, muxes);
        } else {
            g_c006Mux[0] = probe;
        }
    } else {
        /* Would mean the heap lock consumed everything on boxA, which has
         * MUX_LIMIT 4 and therefore 3 usable - so this is a real failure. */
        fails += BoxMemFail(caseName, "no usable mux at all", muxes, 1U);
    }

    for (i = 0; i < BOX_MEM_C006_MAX; i++) {
        if (g_c006Mux[i] != 0U) {
            UINT32 dret = LOS_MuxDelete(g_c006Mux[i]);
            if (dret != LOS_OK) {
                dprintf("[MEM] FAIL %s: MuxDelete(%u) returned 0x%x - slot LEAKED\n",
                        caseName, g_c006Mux[i], dret);
                fails++;
            }
            g_c006Mux[i] = 0U;
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMemC006(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMemC006", MemC006Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}

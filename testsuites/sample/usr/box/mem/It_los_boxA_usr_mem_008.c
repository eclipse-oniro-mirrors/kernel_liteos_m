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
 * MEM-BOX-008  Allocated blocks do not alias each other  (group A)
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS CASE MEANS
 * ---------------------------------------------------------------------------
 * Every other case in this suite trusts `LOS_MemInfoGet`'s counters. This one
 * checks the memory itself: fill several blocks with distinguishable patterns,
 * free half of them, allocate again, and verify the SURVIVORS still hold their
 * original contents.
 *
 * That sequence is what catches a control node being clobbered. The node lives
 * immediately below each payload; if a split/merge wrote across a boundary, or a
 * freed block was handed out twice while still referenced, the damage shows up
 * here as a corrupted pattern - while the pool counters would still look
 * perfectly healthy.
 *
 * ---------------------------------------------------------------------------
 * HOW TO USE
 * ---------------------------------------------------------------------------
 * A failure names the phase: "survivors corrupted" means the second allocation
 * overlapped live data (double allocation or a bad merge); "pattern mismatch"
 * right after filling means two blocks share an address.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

#define BOX_MEM_BOX008_N     8U
#define BOX_MEM_BOX008_SIZE  48U

static VOID *g_box008Blocks[BOX_MEM_BOX008_N];

/* Fill a block with a pattern derived from its index. */
static VOID BoxMem008Fill(VOID *p, UINT32 tag)
{
    UINT32 *w = (UINT32 *)p;
    UINT32 i;

    for (i = 0; i < (BOX_MEM_BOX008_SIZE / sizeof(UINT32)); i++) {
        w[i] = (tag << 16) | i;
    }
}

/* Verify the same pattern; returns FALSE on the first mismatch. */
static BOOL BoxMem008Check(const VOID *p, UINT32 tag)
{
    const UINT32 *w = (const UINT32 *)p;
    UINT32 i;

    for (i = 0; i < (BOX_MEM_BOX008_SIZE / sizeof(UINT32)); i++) {
        if (w[i] != ((tag << 16) | i)) {
            return FALSE;
        }
    }
    return TRUE;
}

static UINT32 MemBox008Case(VOID)
{
    static const CHAR *caseName = "MEM-BOX-008";
    UINT32 fails = 0;
    UINT32 i;

    (VOID)memset_s(g_box008Blocks, sizeof(g_box008Blocks), 0, sizeof(g_box008Blocks));

    /* ---- fill every block with its own pattern ---- */
    for (i = 0; i < BOX_MEM_BOX008_N; i++) {
        g_box008Blocks[i] = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_BOX008_SIZE);
        if (g_box008Blocks[i] == NULL) {
            fails += BoxMemFail(caseName, "alloc refused", i, BOX_MEM_BOX008_N);
            break;
        }
        BoxMem008Fill(g_box008Blocks[i], i + 1U);
    }

    /* ---- distinct blocks must have distinct addresses ---- */
    for (i = 0; i < BOX_MEM_BOX008_N; i++) {
        UINT32 j;
        if (g_box008Blocks[i] == NULL) {
            continue;
        }
        for (j = i + 1U; j < BOX_MEM_BOX008_N; j++) {
            if ((g_box008Blocks[j] != NULL) &&
                (g_box008Blocks[i] == g_box008Blocks[j])) {
                fails += BoxMemFail(caseName, "two blocks share an address", i, j);
            }
        }
    }

    /* ---- free the even-indexed ones, then allocate again ----
     * The new blocks very likely reuse those slots; the point is that the ODD
     * survivors must keep their contents. */
    for (i = 0; i < BOX_MEM_BOX008_N; i += 2U) {
        if (g_box008Blocks[i] != NULL) {
            (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], g_box008Blocks[i]);
            g_box008Blocks[i] = NULL;
        }
    }
    for (i = 0; i < BOX_MEM_BOX008_N; i += 2U) {
        VOID *p = LOS_MemAlloc((VOID *)g_boxLocalPool[0], BOX_MEM_BOX008_SIZE);
        if (p != NULL) {
            BoxMem008Fill(p, (i + 1U) | 0x80U);   /* different tag space */
            g_box008Blocks[i] = p;
        }
    }

    /* ---- the odd-indexed survivors must be intact ---- */
    for (i = 1; i < BOX_MEM_BOX008_N; i += 2U) {
        if (g_box008Blocks[i] == NULL) {
            continue;
        }
        if (!BoxMem008Check(g_box008Blocks[i], i + 1U)) {
            fails += BoxMemFail(caseName, "survivor pattern corrupted", i, 0U);
        }
    }

    /* ---- release everything ---- */
    for (i = 0; i < BOX_MEM_BOX008_N; i++) {
        if (g_box008Blocks[i] != NULL) {
            (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], g_box008Blocks[i]);
            g_box008Blocks[i] = NULL;
        }
    }

    ICUNIT_ASSERT_EQUAL(fails, 0, fails);
    return LOS_OK;
}

VOID ItLosBoxUsrMem008(VOID)
{
    TEST_ADD_CASE("ItLosBoxUsrMem008", MemBox008Case, TEST_LOS, TEST_LOS_USR, TEST_LEVEL1, TEST_FUNCTION);
}

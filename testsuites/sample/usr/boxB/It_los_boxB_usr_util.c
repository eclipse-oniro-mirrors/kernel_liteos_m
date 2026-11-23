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
 * It_los_boxB_usr_util.c - the single definition of the boxB heap helpers.
 *
 * Ordinary (non-inlined) functions on purpose, same reasoning as the boxA
 * suite: one definition instead of one copy per TU, breakpointable bodies, and
 * -ffunction-sections + --gc-sections keeps unused ones out of the image.
 *
 * The measurement model is identical to boxA's because both boxes run the SAME
 * bestfit allocator out of the COMMON sections; only the pool differs.
 * ========================================================================== */

#include "It_los_boxB_usr_util.h"

BOOL BoxBSnapGet(BoxBSnap *snap)
{
    LOS_MEM_POOL_STATUS st = {0};
    UINTPTR pool;

    if (snap == NULL) {
        return FALSE;
    }
    (VOID)memset_s(snap, sizeof(BoxBSnap), 0, sizeof(BoxBSnap));

    pool = g_boxLocalPool[0];
    if (pool == 0) {
        BOXB_TRACE("[BOXB] snapshot: no box context\n");
        return FALSE;
    }
    snap->pool = pool;

    if (LOS_MemInfoGet((VOID *)pool, &st) != LOS_OK) {
        BOXB_TRACE("[BOXB] snapshot: pool 0x%x refused\n", (UINT32)pool);
        return FALSE;
    }

    snap->poolSize  = LOS_MemPoolSizeGet((const VOID *)pool);
    snap->used      = st.totalUsedSize;
    snap->freeSize  = st.totalFreeSize;
    snap->maxFree   = st.maxFreeNodeSize;
    snap->usedNodes = st.usedNodeNum;
    snap->freeNodes = st.freeNodeNum;
    /* Bytes a caller may actually request from the biggest free node: the node's
     * own header is part of its recorded size. */
    snap->payload   = (snap->freeSize > BOXB_NODE_HEAD_SIZE) ?
                      (snap->freeSize - BOXB_NODE_HEAD_SIZE) : 0;
    snap->valid     = TRUE;

    BOXB_TRACE("[BOXB] pool=0x%x size=%u used=%u free=%u maxFree=%u "
               "payload=%u usedNodes=%u freeNodes=%u\n",
               (UINT32)snap->pool, snap->poolSize, snap->used,
               snap->freeSize, snap->maxFree, snap->payload,
               snap->usedNodes, snap->freeNodes);
    return TRUE;
}

BOOL BoxBSame(const BoxBSnap *a, const BoxBSnap *b)
{
    if ((a == NULL) || (b == NULL) || !a->valid || !b->valid) {
        return FALSE;
    }
    return (BOOL)((a->pool == b->pool) && (a->poolSize == b->poolSize) &&
                  (a->used == b->used) && (a->freeSize == b->freeSize) &&
                  (a->maxFree == b->maxFree) && (a->usedNodes == b->usedNodes) &&
                  (a->freeNodes == b->freeNodes));
}

UINT32 BoxBFail(const CHAR *caseName, const CHAR *what,
                UINT32 actual, UINT32 expect)
{
    /* Always printed (both decimal and hex, because pool addresses are only
     * readable in hex and sizes only in decimal). */
    BOXB_FAIL_LOG("[BOXB] FAIL %s: %s (actual=%u/0x%x expect=%u/0x%x)\n",
                  caseName, what, actual, actual, expect, expect);
    return 1U;
}

UINT32 BoxBAllocCost(UINT32 size)
{
    return (UINT32)OS_MEM_ALIGN(size + BOXB_NODE_HEAD_SIZE, OS_MEM_ALIGN_SIZE);
}

UINT32 BoxBAlignAllocCost(UINT32 size, UINT32 boundary)
{
    UINT32 useSize = (size + boundary) - (UINT32)sizeof(UINT32);

    return (UINT32)OS_MEM_ALIGN(useSize + BOXB_NODE_HEAD_SIZE, OS_MEM_ALIGN_SIZE);
}

VOID BoxBSnapDump(const BoxBSnap *s, const CHAR *tag)
{
    if ((s == NULL) || !s->valid) {
        return;
    }
    BOXB_TRACE("[BOXB] %s: pool=0x%x size=%u used=%u free=%u maxFree=%u "
               "payload=%u usedNodes=%u freeNodes=%u\n",
               tag, (UINT32)s->pool, s->poolSize, s->used, s->freeSize,
               s->maxFree, s->payload, s->usedNodes, s->freeNodes);
}

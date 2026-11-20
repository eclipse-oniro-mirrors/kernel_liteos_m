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
 * It_los_boxA_usr_mem_util.c - the single definition of the shared memory-case
 * helpers declared in It_los_boxA_usr_mem_util.h.
 *
 * Ordinary (non-inlined) functions on purpose. The reasons are in the header:
 * one place to fix, breakpointable bodies, readable call sites. Two properties
 * keep that affordable:
 *
 *   - the boxA module is compiled with -ffunction-sections and linked with
 *     --gc-sections, so a helper no case calls is dropped entirely;
 *   - the file is silent unless BOX_MEM_VERBOSE is defined (the case files read
 *     their verdict from the iCunit [Passed]/[Failed] rows), so the format
 *     strings cost nothing by default.
 *
 * Being a normal object also means these four public symbols appear in the
 * image (BoxMemSnapGet / BoxMemSame / BoxMemFail / BoxMemTskCreate / ...); they
 * live in the boxA slot with the cases, so they are unreachable from the kernel
 * world - which is the correct place for them.
 * ========================================================================== */

#include "It_los_boxA_usr_mem_util.h"

/* ---------------------------------------------------------------------------
 * Snapshot
 * ------------------------------------------------------------------------- */
BOOL BoxMemSnapGet(BoxMemSnap *snap, const CHAR *tag)
{
    LOS_MEM_POOL_STATUS st = {0};
    UINTPTR pool;

    if (snap == NULL) {
        return FALSE;
    }
    (VOID)memset_s(snap, sizeof(BoxMemSnap), 0, sizeof(BoxMemSnap));

    pool = g_boxLocalPool[0];
    if (pool == 0) {
        BOX_MEM_TRACE("[MEM] %s: no box context\n", tag);
        return FALSE;
    }
    snap->pool = pool;

    if (LOS_MemInfoGet((VOID *)pool, &st) != LOS_OK) {
        BOX_MEM_TRACE("[MEM] %s: pool 0x%x refused\n", tag, (UINT32)pool);
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
    snap->payload   = (snap->freeSize > BOX_MEM_NODE_HEAD_SIZE) ?
                      (snap->freeSize - BOX_MEM_NODE_HEAD_SIZE) : 0;
    snap->valid     = TRUE;

    BOX_MEM_TRACE("[MEM] %s: pool=0x%x size=%u used=%u free=%u maxFree=%u "
                  "payload=%u usedNodes=%u freeNodes=%u\n",
                  tag, (UINT32)snap->pool, snap->poolSize, snap->used,
                  snap->freeSize, snap->maxFree, snap->payload,
                  snap->usedNodes, snap->freeNodes);
    return TRUE;
}

BOOL BoxMemSame(const BoxMemSnap *a, const BoxMemSnap *b)
{
    if ((a == NULL) || (b == NULL) || !a->valid || !b->valid) {
        return FALSE;
    }
    return (BOOL)((a->pool == b->pool) && (a->poolSize == b->poolSize) &&
                  (a->used == b->used) && (a->freeSize == b->freeSize) &&
                  (a->maxFree == b->maxFree) && (a->usedNodes == b->usedNodes) &&
                  (a->freeNodes == b->freeNodes));
}

UINT32 BoxMemFail(const CHAR *caseName, const CHAR *what,
                  UINT32 actual, UINT32 expect)
{
    /* One line per broken invariant, so a run names them all instead of stopping
     * at the first. Uses the always-on failure macro: a failure that cannot be
     * read is worse than no test at all. */
    BOX_MEM_FAIL_LOG("[MEM] FAIL %s: %s (actual=%u/0x%x expect=%u/0x%x)\n",
                     caseName, what, actual, actual, expect, expect);
    return 1U;
}

/* ---------------------------------------------------------------------------
 * Allocation-cost formulas - mirror los_memory.c so the cases never hard-code a
 * size. Cross-checked by MEM-BOX-003/004, which measure them directly.
 * ------------------------------------------------------------------------- */
UINT32 BoxMemAllocCost(UINT32 size)
{
    return (UINT32)OS_MEM_ALIGN(size + BOX_MEM_NODE_HEAD_SIZE, OS_MEM_ALIGN_SIZE);
}

UINT32 BoxMemAlignAllocCost(UINT32 size, UINT32 boundary)
{
    UINT32 useSize = (size + boundary) - (UINT32)sizeof(UINT32);

    return (UINT32)OS_MEM_ALIGN(useSize + BOX_MEM_NODE_HEAD_SIZE, OS_MEM_ALIGN_SIZE);
}

/* ---------------------------------------------------------------------------
 * Probe tasks
 * ------------------------------------------------------------------------- */
UINT32 BoxMemTskCreate(TSK_ENTRY_FUNC entry, const CHAR *name,
                       UINT32 stackSize, UINT32 *taskId)
{
    TSK_INIT_PARAM_S param;

    if ((entry == NULL) || (taskId == NULL)) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    (VOID)memset_s(&param, sizeof(param), 0, sizeof(param));
    param.pfnTaskEntry = entry;
    param.uwStackSize  = stackSize;
    param.pcName       = (CHAR *)name;
    param.usTaskPrio   = BOX_MEM_TSK_PRIO;
    param.uwResved     = 0;          /* detached: recycled on exit */

    return LOS_TaskCreate(taskId, &param);
}

BOOL BoxMemTskWaitFlag(const volatile UINT32 *flag, UINT32 expect)
{
    UINT32 spin;

    if (flag == NULL) {
        return FALSE;
    }
    for (spin = 0; (*flag != expect) && (spin < BOX_MEM_TSK_SPIN); spin++) {
        LOS_TaskDelay(BOX_MEM_TSK_SPIN_TICKS);
    }
    return (BOOL)(*flag == expect);
}

BOOL BoxMemTskSettle(const BoxMemSnap *baseline, BoxMemSnap *out)
{
    BoxMemSnap cur;
    UINT32 spin;

    for (spin = 0; spin < BOX_MEM_TSK_SPIN; spin++) {
        LOS_TaskDelay(BOX_MEM_TSK_SPIN_TICKS);
        LOS_TaskStackRecycle();          /* give the queued stacks back */
        if (!BoxMemSnapGet(&cur, "settle")) {
            break;
        }
        if (BoxMemSame(baseline, &cur)) {
            if (out != NULL) {
                *out = cur;
            }
            return TRUE;
        }
    }
    if (out != NULL) {
        (VOID)BoxMemSnapGet(out, "settle-final");
    }
    return FALSE;
}

BOOL BoxMemPatternCheck(UINT8 *buf, UINT32 len, UINT32 seed)
{
    UINT32 words;
    UINT32 i;

    if ((buf == NULL) || (len < sizeof(UINT32))) {
        return FALSE;
    }
    words = len / (UINT32)sizeof(UINT32);
    for (i = 0; i < words; i++) {
        volatile UINT32 *p = (volatile UINT32 *)(VOID *)buf + i;
        *p = seed ^ (i * 0x01010101U);
    }
    for (i = 0; i < words; i++) {
        volatile UINT32 *p = (volatile UINT32 *)(VOID *)buf + i;
        if (*p != (seed ^ (i * 0x01010101U))) {
            return FALSE;
        }
    }
    return TRUE;
}

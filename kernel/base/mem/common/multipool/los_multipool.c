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

#include "los_memory_pri.h"
#ifdef LOSCFG_MEM_MUL_POOL_ALLOC
#include "los_memory_internal.h"
#endif
#include "los_hook.h"
#ifdef LOSCFG_KERNEL_LMS
#include "los_lms_pri.h"
#endif

STATIC VOID *g_poolHead = NULL;

#ifdef LOSCFG_BOX
/* Per-box pool registries (refs kernel_s multipool shape): one list head per
 * box, over the pools that box created. This is what makes the feature usable
 * under BOX at all - the overlap check below dereferences every registered
 * pool (LOS_MemPoolSizeGet reads the pool header), so a box walking the
 * KERNEL registry would read kernel-heap memory it has no mapping for and
 * take a BusFault. With per-box heads a box only ever walks its own list.
 *
 * Both dispatch inputs are user-readable: g_sysMode (behind LOS_IN_KERNEL,
 * declared in los_memory_pri.h) and g_curBoxId live in the R2 section
 * (los_usr_init.c). g_curBoxId is declared locally rather than pulled from
 * los_box_pri.h because that header and los_memory_pri.h form an include
 * cycle - the same reason los_memory_pri.h declares g_sysMode locally. */
VOID *g_usrBoxPoolHead[LOSCFG_BASE_BOX_MAX_LIMIT] = {0};

extern volatile UINT32 g_curBoxId;

/* refs ANDs OS_SCHEDULER_ACTIVE into this predicate. Dropped here on purpose:
 * it expands to a read of g_taskScheduled (kernel RAM), so evaluating it from
 * user mode faults - and because it is the SECOND operand of an && whose
 * first operand is true exactly in user mode, it would always be evaluated
 * on the side that cannot read it. It is also redundant: g_sysMode only
 * leaves its kernel default once the scheduler has switched a box task in, so
 * !LOS_IN_KERNEL already implies an active scheduler. The bound check is the
 * safety net against a stale OS_KERNEL_BOX (0xFF) reaching the array. */
STATIC INLINE VOID **OsMemMulPoolHead(VOID)
{
    if (!LOS_IN_KERNEL && (g_curBoxId < LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return &g_usrBoxPoolHead[g_curBoxId];
    }
    return &g_poolHead;
}

/* OsBoxDestroy callee: drop the destroyed box's registry so a restarted box
 * begins with an empty list (its entry task re-runs LOS_MemInit, which would
 * otherwise fail the overlap check against its own stale registration). */
VOID OsBoxMulPoolHeadClear(UINT32 boxId)
{
    if (boxId >= LOSCFG_BASE_BOX_MAX_LIMIT) {
        return;
    }
    g_usrBoxPoolHead[boxId] = NULL;
}
#else
/* Baseline arm: one registry, nothing to dispatch on. Spelled as a second
 * definition rather than an #ifdef at each caller so the two call sites below
 * stay unconditional. Returning the address of a file-scope static from a
 * STATIC INLINE folds away completely - the BOX=off object is byte-identical
 * to the upstream single-registry code. */
STATIC INLINE VOID **OsMemMulPoolHead(VOID)
{
    return &g_poolHead;
}
#endif /* LOSCFG_BOX */

UINT32 OsMemMulPoolInit(VOID *pool, UINT32 size)
{
    /* Which registry this context registers into: the running box's under
     * BOX, the single kernel one otherwise. */
    VOID **head = OsMemMulPoolHead();
    VOID *nextPool = *head;
    VOID *curPool = *head;
    UINTPTR poolEnd;

    while (nextPool != NULL) {
        poolEnd = (UINTPTR)nextPool + LOS_MemPoolSizeGet(nextPool);
        if (((pool <= nextPool) && (((UINTPTR)pool + size) > (UINTPTR)nextPool)) ||
            (((UINTPTR)pool < poolEnd) && (((UINTPTR)pool + size) >= poolEnd))) {
            PRINT_ERR("Failed to create pool: address overlaps\n");
            return LOS_NOK;
        }
        curPool = nextPool;
        nextPool = ((LosMemPoolInfo *)nextPool)->nextPool;
    }

    if (*head == NULL) {
        *head = pool;
    } else {
        ((LosMemPoolInfo *)curPool)->nextPool = pool;
    }
    ((LosMemPoolInfo *)pool)->nextPool = NULL;

    return LOS_OK;
}

UINT32 OsMemMulPoolDeinit(const VOID *pool)
{
    UINT32 ret = LOS_NOK;
    VOID *nextPool = NULL;
    VOID *curPool = NULL;
    VOID **head = OsMemMulPoolHead();

    do {
        if (pool == NULL) {
            break;
        }

        if (pool == *head) {
            *head = ((LosMemPoolInfo *)(*head))->nextPool;
            ret = LOS_OK;
            break;
        }

        curPool = *head;
        nextPool = *head;
        while (nextPool != NULL) {
            if (pool == nextPool) {
                ((LosMemPoolInfo *)curPool)->nextPool = ((LosMemPoolInfo *)nextPool)->nextPool;
                ret = LOS_OK;
                break;
            }
            curPool = nextPool;
            nextPool = ((LosMemPoolInfo *)nextPool)->nextPool;
        }
    } while (0);

    return ret;
}

/* Kernel-side reader: returns the KERNEL registry head, and stays that way.
 * Its only caller is OsMemExcInfoGet (los_exc.c), which runs in kernel
 * exception context and wants the kernel pools. A box must never call this -
 * walking the returned list would dereference kernel pool headers. */
VOID *OsMemMulPoolHeadGet(VOID)
{
    return g_poolHead;
}

LITE_OS_SEC_TEXT_INIT UINT32 LOS_MemDeInit(VOID *pool)
{
    UINT32 ret;
    UINT32 intSave = 0;

    if (pool == NULL) {
        return LOS_NOK;
    }

#ifdef LOSCFG_KERNEL_LMS
     if (g_lms != NULL) {
         g_lms->deInit(pool);
     }
#endif

#if !defined(LOSCFG_KERNEL_MEM_TLSF)
    MEM_LOCK(intSave, pool);
    ret = OsMemMulPoolDeinit(pool);
    MEM_UNLOCK(intSave, pool);
#else
    MEM_LOCK(pool, intSave);
    ret = OsMemMulPoolDeinit(pool);
    MEM_UNLOCK(pool, intSave);
#endif

#ifdef LOSCFG_BOX
    if (ret == LOS_OK) {
        (VOID)MemLockDelete(pool);
        ((LosMemPoolInfo *)pool)->memMux = OS_INVALID;
    }
#endif

	OsHookCall(LOS_HOOK_TYPE_MEM_DEINIT, pool);

    return ret;
}

/* Kernel-side listing, like OsMemMulPoolHeadGet above: it walks the kernel
 * registry and calls OsMemInfoPrint on each pool, so it is not box-callable.
 * Left on the kernel head deliberately (refs does the same). */
LITE_OS_SEC_TEXT_INIT UINT32 LOS_MemPoolList(VOID)
{
    VOID *nextPool = g_poolHead;
    UINT32 index = 0;

    while (nextPool != NULL) {
        PRINTK("pool%u :\n", index);
        index++;
        OsMemInfoPrint(nextPool);
        nextPool = ((LosMemPoolInfo *)nextPool)->nextPool;
    }
    return index;
}

#ifdef LOSCFG_MEM_MUL_POOL_ALLOC
typedef struct {
    ALLOC_PROC_FUNC allocMemPoolFunc;
    FREE_PROC_FUNC freeMemPoolFunc;
} MultiPoolOps;

#define MULTPOOL_HOOK_NOT_REGISTER  0
#define MULTPOOL_HOOK_REGISTER      1

STATIC MultiPoolOps g_poolOps;
UINT32 g_mulPoolMux = 0;
UINT32 g_poolFlag = MULTPOOL_HOOK_NOT_REGISTER;

LITE_OS_SEC_TEXT UINT32 LOS_MulPoolRegister(ALLOC_PROC_FUNC allocFunc, FREE_PROC_FUNC freeFunc)
{
    if (g_poolFlag == MULTPOOL_HOOK_REGISTER) {
        return LOS_NOK;
    }

    if (allocFunc == NULL || freeFunc == NULL) {
        return LOS_NOK;
    }

    if (LOS_MuxCreate(&g_mulPoolMux) != LOS_OK) {
        return LOS_NOK;
    }

    g_poolOps.allocMemPoolFunc = allocFunc;
    g_poolOps.freeMemPoolFunc = freeFunc;
    g_poolFlag = MULTPOOL_HOOK_REGISTER;

    return LOS_OK;
}

STATIC LITE_OS_SEC_TEXT VOID *OsMulPoolExpand(UINT32 size)
{
    UINT32 ret = LOS_NOK;
    VOID *nextPool = NULL;
    UINTPTR poolAddr = 0;
    UINT32 poolSize = size;

    // Update the value based on the pool size of each memory pool configured for the product.
    g_poolOps.allocMemPoolFunc(&poolAddr, &poolSize);
    if (poolAddr == 0) {
        return NULL;
    }
    nextPool = (VOID *)poolAddr;

    ret = LOS_MemInit(nextPool, poolSize);
    if (ret != LOS_OK) {
        g_poolOps.freeMemPoolFunc(nextPool);
        return NULL;
    }

    return nextPool;
}

LITE_OS_SEC_TEXT VOID *LOS_MulPoolAlloc(UINT32 size)
{
    VOID *ptr = NULL;
    VOID *nextPool = NULL;

    if (OS_INT_ACTIVE) {
        return NULL;
    }

    if (g_poolFlag == MULTPOOL_HOOK_NOT_REGISTER) {
        PRINT_ERR("need register hook\n");
        return NULL;
    }

    LOS_MuxPend(g_mulPoolMux, LOS_WAIT_FOREVER);
    nextPool = ((LosMemPoolInfo *)g_poolHead)->nextPool;

    while (nextPool) {
        ptr = LOS_MemAlloc(nextPool, size);
        if (ptr != NULL) {
            LOS_MuxPost(g_mulPoolMux);
            return ptr;
        }
        nextPool = ((LosMemPoolInfo *)nextPool)->nextPool;
    }

    nextPool = OsMulPoolExpand(size);
    if (nextPool != NULL) {
        ptr = LOS_MemAlloc(nextPool, size);
    } else {
        ptr = NULL;
    }
    LOS_MuxPost(g_mulPoolMux);
    return ptr;
}

LITE_OS_SEC_TEXT VOID *LOS_MulPoolRealloc(VOID *ptr, UINT32 size)
{
    VOID *newPtr = NULL;
    VOID *retPtr = NULL;
    VOID *nextPool = NULL;
    UINTPTR poolEnd;

    if (OS_INT_ACTIVE) {
        return NULL;
    }

    if (ptr == NULL) {
        return LOS_MulPoolAlloc(size);
    }

    if (g_poolFlag == MULTPOOL_HOOK_NOT_REGISTER) {
        PRINT_ERR("need register hook\n");
        return NULL;
    }

    LOS_MuxPend(g_mulPoolMux, LOS_WAIT_FOREVER);
    nextPool = ((LosMemPoolInfo *)g_poolHead)->nextPool;
    while (nextPool != NULL) {
        poolEnd = (UINTPTR)nextPool + LOS_MemPoolSizeGet(nextPool);
        if (((UINTPTR)ptr > (UINTPTR)nextPool) && ((UINTPTR)ptr < (UINTPTR)poolEnd)) {
            retPtr = LOS_MemRealloc(nextPool, ptr, size);
            if (size == 0) {
                LOS_MuxPost(g_mulPoolMux);
                return retPtr;
            }
            if (retPtr == NULL) {
                break;
            }
            LOS_MuxPost(g_mulPoolMux);
            return retPtr;
        }
        nextPool = ((LosMemPoolInfo *)nextPool)->nextPool;
    }

    if (nextPool == NULL) {
        LOS_MuxPost(g_mulPoolMux);
        return NULL;
    }

    newPtr = LOS_MulPoolAlloc(size);
    if (newPtr == NULL) {
        PRINT_ERR("multpool alloc failed\n");
        LOS_MuxPost(g_mulPoolMux);
        return NULL;
    }

    retPtr = OsMulPoolCopyToNewNode(nextPool, ptr, size, newPtr);
    if (retPtr != NULL) {
        LOS_MulPoolFree(ptr);
    } else {
        LOS_MulPoolFree(newPtr);
    }
    LOS_MuxPost(g_mulPoolMux);
    return retPtr;
}

STATIC BOOL OsCheckPoolNoUsed(VOID *pool)
{
    LosMemPoolInfo *poolInfo = (LosMemPoolInfo *)pool;
    LosMemDynNode *headNode = OS_MEM_FIRST_NODE(pool);
    LosMemDynNode *endNode = OS_MEM_END_NODE(pool, poolInfo->poolSize);

    if ((endNode != OS_MEM_NEXT_NODE(headNode)) ||
        (OS_MEM_NODE_GET_USED_FLAG(headNode->selfNode.sizeAndFlag) == OS_MEM_NODE_USED_FLAG)) {
        return FALSE;
    }
    return TRUE;
}

LITE_OS_SEC_TEXT STATIC VOID OsMulPoolShrink(VOID* pool)
{
    if (OsCheckPoolNoUsed(pool) != TRUE) {
        return;
    }

    if (LOS_MemDeInit(pool) != LOS_OK) {
        PRINT_ERR("noused pool deinit failed\n");
        return;
    }

    g_poolOps.freeMemPoolFunc(pool);
}

LITE_OS_SEC_TEXT UINT32 LOS_MulPoolShrink(VOID)
{
    VOID *nextPool = NULL;
    VOID *curPool = NULL;

    if (OS_INT_ACTIVE) {
        return LOS_NOK;
    }

    if (g_poolFlag == MULTPOOL_HOOK_NOT_REGISTER) {
        PRINT_ERR("need register hook\n");
        return LOS_NOK;
    }

    LOS_MuxPend(g_mulPoolMux, LOS_WAIT_FOREVER);
    nextPool = ((LosMemPoolInfo *)g_poolHead)->nextPool;
    while (nextPool) {
        curPool = nextPool;
        nextPool = ((LosMemPoolInfo *)nextPool)->nextPool;
        OsMulPoolShrink(curPool);
    }
    LOS_MuxPost(g_mulPoolMux);

    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 LOS_MulPoolFree(VOID *ptr)
{
    UINT32 ret = LOS_NOK;
    VOID *nextPool = NULL;

    if (OS_INT_ACTIVE) {
        return LOS_NOK;
    }

    if (ptr == NULL) {
        PRINT_ERR("free failed, ptr is NULL\n");
        return LOS_NOK;
    }

    if (g_poolFlag == MULTPOOL_HOOK_NOT_REGISTER) {
        PRINT_ERR("need register hook\n");
        return LOS_NOK;
    }

    LOS_MuxPend(g_mulPoolMux, LOS_WAIT_FOREVER);
    nextPool = ((LosMemPoolInfo *)g_poolHead)->nextPool;
    while (nextPool) {
        if (ptr > nextPool &&
            ptr < (VOID *)((UINTPTR)nextPool + LOS_MemPoolSizeGet(nextPool))) {
            ret = LOS_MemFree(nextPool, ptr);
            LOS_MuxPost(g_mulPoolMux);
            return ret;
        }
        nextPool = ((LosMemPoolInfo *)nextPool)->nextPool;
    }
    LOS_MuxPost(g_mulPoolMux);
    return ret;
}
#endif
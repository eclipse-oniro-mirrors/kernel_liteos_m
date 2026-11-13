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

#ifndef _LOS_MEMORY_PRI_H
#define _LOS_MEMORY_PRI_H

#include "los_memory.h"
#include "los_memstat_pri.h"
#include "los_slab_pri.h"
#include "los_spinlock.h"
#include "los_misc_pri.h"
#include "los_debug.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Memory pool information structure */
#if defined(LOSCFG_KERNEL_MEM_BESTFIT)

typedef struct {
    VOID        *pool;      /* Starting address of a memory pool */
    UINT32      poolSize;   /* Memory pool size */
#ifdef LOSCFG_BOX
    /* refs bestfit LosMemPoolInfo: memMux sits flat right after poolSize
     * (refs los_memory_pri.h:31-44). Gated: BOX=off keeps the OHOS layout
     * byte-identical; all offset macros (OS_MEM_HEAD_ADDR /
     * OS_MEM_FIRST_NODE ...) are sizeof-driven and follow along. */
    UINT32      memMux;     /* pool lock handle: OS_INVALID == int lock */
#endif

#ifdef LOSCFG_MEM_TASK_STAT
    Memstat     stat;
#endif

#if (LOSCFG_MEM_MUL_POOL == 1)
    VOID        *nextPool;
#endif

#ifdef LOSCFG_KERNEL_MEM_SLAB_EXTENTION
    struct LosSlabControlHeader slabCtrlHdr;
#endif
} LosMemPoolInfo;

#elif defined(LOSCFG_KERNEL_MEM_BESTFIT_LITTLE)

typedef struct LosHeapManager {
    struct LosHeapNode *head;
    struct LosHeapNode *tail;
    UINT32 size;

#ifdef LOSCFG_MEM_TASK_STAT
    Memstat stat;
#endif

#if (LOSCFG_MEM_MUL_POOL == 1)
    VOID *nextPool;
#endif

#ifdef LOSCFG_KERNEL_MEM_SLAB_EXTENTION
    struct LosSlabControlHeader slabCtrlHdr;
#endif
} LosMemPoolInfo;

#elif defined(LOSCFG_KERNEL_MEM_TLSF)

/*
 * tlsf keeps its own pool head (struct OsMemPoolHead, declared in los_memory.h);
 * alias it so the common multipool helpers can manage the linked list uniformly.
 */
typedef struct OsMemPoolHead LosMemPoolInfo;

#endif

#define IS_ALIGNED(value, alignSize) ((((UINTPTR)(value)) & ((UINTPTR)((alignSize) - 1))) == 0)

#if !defined(LOSCFG_KERNEL_MEM_TLSF)
/* spinlock for mem module, only available on SMP mode */
extern SPIN_LOCK_S g_memSpin;
#define MEM_LOCK(intSave, pool)       LOS_SpinLockSave(&g_memSpin, &(intSave))
#define MEM_UNLOCK(intSave, pool)     LOS_SpinUnlockRestore(&g_memSpin, intSave)
#else
/* The memory pool support expand. */
#define OS_MEM_POOL_EXPAND_ENABLE   0x01
/* The memory pool support no lock. */
#define OS_MEM_POOL_UNLOCK_ENABLE   0x02
extern SPIN_LOCK_S g_memSpin;
#define MEM_LOCK(pool, state)       do {                    \
    if (!(((struct OsMemPoolHead *)(pool))->info.attr & OS_MEM_POOL_UNLOCK_ENABLE)) { \
        LOS_SpinLockSave(&g_memSpin, &(state));             \
    }                                                       \
} while (0);
#define MEM_UNLOCK(pool, state)     do {                    \
    if (!(((struct OsMemPoolHead *)(pool))->info.attr & OS_MEM_POOL_UNLOCK_ENABLE)) { \
        LOS_SpinUnlockRestore(&g_memSpin, state);           \
    }                                                       \
} while (0);
#endif

/* ---- box heap lock family (refs los_memory_pri.h:67-142) ----
 * Selects between a mux lock and an int lock per pool via the memMux field
 * (OS_INVALID == int lock). M5D: the family follows the refs bestfit FLAT
 * memMux field (refs los_memory_pri.h:31-44); bestfit is the ported
 * algorithm of record (refs mem/bestfit) — TLSF is not adapted, and a
 * BOX+TLSF build fails here on the missing flat field (by design). */
#ifdef LOSCFG_BOX
#include "los_mux.h"

#define OS_MEM_LOCK_TYPE_MUX             0U
#define OS_MEM_LOCK_TYPE_INT             LOS_MEM_INIT_ATTR_LOCK_MSK

STATIC_INLINE UINT32 OsMemLockModeGet(UINT32 attr, BOOL *intLock)
{
    if ((attr & LOS_MEM_INIT_ATTR_LOCK_MSK) == OS_MEM_LOCK_TYPE_MUX) {
        *intLock = FALSE;
    } else if ((attr & LOS_MEM_INIT_ATTR_LOCK_MSK) == OS_MEM_LOCK_TYPE_INT) {
        *intLock = TRUE;
    } else {
        return LOS_NOK;
    }
    return LOS_OK;
}

#define MEMLOCKCREATE_CALL(type, pool, intLock)    MemLockCreate##type(pool, intLock)
#define MEMLOCKCREATE_FUNCTION(type) \
    STATIC_INLINE UINT32 MemLockCreate##type(VOID *pool, BOOL intLock) \
    { \
        if (intLock) { \
            ((type *)pool)->memMux = OS_INVALID; \
        } else { \
            ((type *)pool)->memMux = 0; \
            if (LOS_MuxCreate(&((type *)pool)->memMux) != LOS_OK) { \
                return LOS_NOK; \
            } \
        } \
        return LOS_OK; \
    }

#define MEMLOCKDELETE_CALL(type, pool)            MemLockDelete##type(pool)
#define MEMLOCKDELETE_FUNCTION(type) \
    STATIC_INLINE UINT32 MemLockDelete##type(const VOID *pool) \
    { \
        if (((type *)pool)->memMux != OS_INVALID) { \
            if (LOS_MuxDelete(((const type *)pool)->memMux) != LOS_OK) { \
                return LOS_NOK; \
            } \
        } \
        return LOS_OK; \
    }

#define MEMLOCK_CALL(type, pool, intSave)         MemLock##type(pool, intSave)
#define MEMLOCK_FUNCTION(type) \
    STATIC_INLINE UINT32 MemLock##type(const VOID *pool, UINT32 *intSave) \
    { \
        UINT32 ret; \
        if (((type *)pool)->memMux != OS_INVALID) { \
            ret = LOS_MuxPend(((type *)pool)->memMux, LOS_WAIT_FOREVER); \
            if (ret != LOS_OK) { \
                return LOS_NOK; \
            } \
        } else { \
            *intSave = LOS_IntLock(); \
        } \
        return LOS_OK; \
    }

#define MEMUNLOCK_CALL(type, pool, intSave)       MemUnlock##type(pool, intSave)
#define MEMUNLOCK_FUNCTION(type) \
    STATIC_INLINE VOID MemUnlock##type(const VOID *pool, UINT32 intSave) \
    { \
        if (((type *)pool)->memMux != OS_INVALID) { \
            (VOID)LOS_MuxPost(((const type *)pool)->memMux); \
        } else { \
            LOS_IntRestore(intSave); \
        } \
    }

/* Instantiate for the bestfit pool head (flat memMux, refs shape). */
MEMLOCKCREATE_FUNCTION(LosMemPoolInfo)
MEMLOCKDELETE_FUNCTION(LosMemPoolInfo)
MEMLOCK_FUNCTION(LosMemPoolInfo)
MEMUNLOCK_FUNCTION(LosMemPoolInfo)
#define MemLockCreate(pool, intLock)   MEMLOCKCREATE_CALL(LosMemPoolInfo, (pool), (intLock))
#define MemLockDelete(pool)            MEMLOCKDELETE_CALL(LosMemPoolInfo, (pool))
#define MemLock(pool, intSave)         MEMLOCK_CALL(LosMemPoolInfo, (pool), (intSave))
#define MemUnlock(pool, intSave)       MEMUNLOCK_CALL(LosMemPoolInfo, (pool), (intSave))

/* M5C/M5D: swap the bestfit alloc/free critical sections to the per-pool
 * lock (refs bestfit los_memory.c uses MemLock/MemUnlock at every
 * alloc-path site). The bestfit call sites keep the (intSave, pool)
 * argument order of the BOX=off spinlock macros above, so the
 * redefinition below preserves that order while routing into the pool
 * lock. refs checks each MemLock return at every call site; the macro
 * expansion cannot early-return, so a MemLock failure — structurally
 * impossible here (WAIT_FOREVER pend on a mux created at pool init,
 * before the pool is visible to any caller) — is swallowed; annotated
 * deviation. */
#undef MEM_LOCK
#undef MEM_UNLOCK
#define MEM_LOCK(intSave, pool)       do { (VOID)MemLock((pool), &(intSave)); } while (0);
#define MEM_UNLOCK(intSave, pool)     do { MemUnlock((pool), (intSave)); } while (0);

/* refs los_memory_pri.h:186-208 — context-driven lock-mode pick for
 * LOS_MemInit pools. LOSCFG_USR_MEM_ALLOC is on in this OHOS base, so a
 * LOS_MemInit pool picks the mux lock outside kernel context. The
 * current-mode global + macro mirror los_box_pri.h verbatim (identical
 * replacement lists re-define benignly; the local extern avoids the
 * los_box_pri.h <-> los_memory_pri.h include cycle). */
extern UINT32 g_sysMode;
#define LOS_IN_KERNEL                   (g_sysMode == 0)   /* refs los_base.h */

STATIC_INLINE BOOL OsMemIntLockCheck(const VOID *pool, UINT32 size)
{
    BOOL intLock = FALSE;
    (VOID)pool;
    (VOID)size;

#ifdef LOSCFG_USR_MEM_ALLOC
#ifdef LOSCFG_KERNEL_MEM_ALLOC
    if (LOS_IN_KERNEL) {
        intLock = TRUE;
    }
#endif
#else
    intLock = TRUE;
#endif
    return intLock;
}

STATIC_INLINE UINT32 OsMemAttrGet(const VOID *pool, UINT32 size)
{
    return OsMemIntLockCheck(pool, size) ?
           (OS_MEM_LOCK_TYPE_INT | LOS_MEM_INIT_ATTR_SLAB_MSK) :
           (OS_MEM_LOCK_TYPE_MUX | LOS_MEM_INIT_ATTR_SLAB_MSK);
}
#endif /* LOSCFG_BOX */

extern UINTPTR g_sys_mem_addr_end;
extern UINT32 OsMemSystemInit(VOID);

/* SLAB extension needs memory algorithms provide following internal apis */
#ifdef LOSCFG_KERNEL_MEM_SLAB_EXTENTION
extern VOID* OsMemAlloc(VOID *pool, UINT32 size);
extern UINT32 OsMemFree(VOID *pool, const VOID *ptr);
#endif /* LOSCFG_KERNEL_MEM_SLAB_EXTENTION */

#ifdef LOSCFG_MAX_POOL_NUM
#define MAX_POOL_NUM LOSCFG_MAX_POOL_NUM
#else
#define MAX_POOL_NUM 8
#endif

#if (LOSCFG_MEM_MUL_POOL == 1)
extern UINT32 OsMemMulPoolInit(VOID *pool, UINT32 size);
extern UINT32 OsMemMulPoolDeinit(const VOID *pool);
extern VOID *OsMemMulPoolHeadGet(VOID);
#ifdef LOSCFG_BOX
/* refs los_memory_pri.h:170 — unlinks the box pools from the global
 * multi-pool list (OsBoxDestroy); implemented in los_multipool.c. */
extern VOID OsBoxMulPoolHeadClear(UINT32 boxId);
#endif
#else /* LOSCFG_MEM_MUL_POOL */
STATIC INLINE UINT32 OsMemMulPoolInit(VOID *pool, UINT32 size)
{
    (VOID)pool;
    (VOID)size;

    return LOS_OK;
}

STATIC INLINE UINT32 OsMemMulPoolDeinit(const VOID *pool)
{
    (VOID)pool;

    return LOS_OK;
}
#endif /* LOSCFG_MEM_MUL_POOL */

#if (LOSCFG_PLATFORM_EXC == 1)
/* tlsf-only: per-node integrity check filling errorAddr/errLen/errOwner */
extern VOID OsMemExcErrInfoGet(VOID *pool, MemInfoCB *memExcInfo);
#endif

extern UINT32 OsPoolInfoGet(VOID *pool, UINT32 *usedNodeNum, UINT32 *freeNodeNum, UINT32 *peak);

#ifdef LOSCFG_EXC_INTERACTION
extern UINT32 OsMemExcInteractionInit(UINTPTR memStart);
#endif

#ifdef LOSCFG_MEM_LEAKCHECK
extern VOID OsMemUsedNodeShow(VOID *pool);
#endif

#ifdef LOSCFG_MEM_DEBUG
VOID OsExcMemInfo(VOID);
VOID OsCheckPool(LosMemPoolInfo *poolInfo);
#endif

extern VOID OsMemResetEndNode(VOID *pool, UINTPTR preAddr);
extern VOID OsMemInfoPrint(const VOID *pool);
extern UINT32 OsShellCmdMemCheck(INT32 argc, const CHAR *argv[]);
extern VOID OsMemIntegrityMultiCheck(VOID);

#if (LOSCFG_TASK_MEM_USED == 1)
extern VOID OsTaskMemUsed(VOID *pool, UINT32 *tskMemInfoBuf, UINT32 tskMemInfoCnt);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_MEMORY_PRI_H */

/*
 * los_sec.c — SID (system identifier) module core.
 *
 * M5C reconstruction: refs kernel_s/base/los_sec.c is NOT part of the
 * provided snapshot — only the contracts in los_sec_pri.h / refs los_sec.h
 * and the usage pattern in refs safe_ipc/los_queue.c are. This file
 * follows those contracts with the reduced config (no IPC_PERM /
 * IPC_RECOVER / BASE_SID_LIST):
 *
 *  - sId layout: see the reconstruction note in los_sec_pri.h
 *    (auto:1 | boxId:7 | type:4 | dataCBIdx:10 | poolIdx:10).
 *
 *  - locking: SID primitives are lock-free; every caller holds the
 *    scheduler lock around a Gen/Bind/Get/Del sequence (the pattern in
 *    refs safe_ipc/los_queue.c, which wraps OsSIdObjGet + OsSIdBind in
 *    SCHEDULER_LOCK). The public LOS_GenSId / LOS_SIdDelete wrappers take
 *    the lock themselves.
 *
 * M5D STUB NOTICE: the SIdObj-pool machinery is PARKED. The full M5C
 * implementation (OsSIdObjAlloc/Free/Get/Bind with per-box pools and
 * module-entity arrays — see git M5C-1..M5C-5) tripped on the pool-lock
 * mux path: MemLockCreate calls the kernel LOS_MuxCreate from box context,
 * whose kernel-table dataCBIdx bound against the calling box's module
 * entity array and overflowed its listSize (boxB: mux limit 1). Per the
 * porting decision the SID pools are now stubbed while the framework
 * stays: handles are FLAT (auto:1 | boxId:7 | type:4 | idx:10 — no pool
 * object, no entity binding), the owning pool is inferred from the CB
 * address at generation, and OsSIdGetDataCB resolves the CB back by
 * indexing the kernel mux table or the per-box mux pool directly. Cross
 * box owner checks (OsSIdOwnerCheck) are open; stale-handle detection
 * relies on the mux CB lifetime (muxId re-stamped at reuse). The parked
 * functions keep their signatures so the real SID module can drop in.
 * M5E STUB EXTENSION (IPC triad scaffolding, 2026-11-12): the flat-handle
 * stub now covers SEM/QUE with the same two-bound shape as MUX (kernel
 * table + per-box pool: sem g_osAllSem/boxResSemList, queue g_allQueue/
 * boxResQueList) so the upcoming safe_ipc port (refs kernel_s/base/
 * safe_ipc — LOS_EventCreate/PollWithSId & friends) can call
 * OsSIdGenObj/OsSIdGetDataCB without touching the stub again. EVT keeps a
 * framework-only arm: the OHOS baseline has NO kernel event pool (event
 * CBs are user-embedded) and the refs safe_ipc pools (g_osAllEvent,
 * LosEventCB) do not exist yet — generation stamps the kernel head
 * unconditionally and resolution returns INVALID until the safe_ipc port
 * lands its own pools and tightens both arms. All of this remains
 * FLAT: no pool object, no entity binding; the parked functions keep
 * their signatures so the real SID module can drop in.
 */
#include "los_typedef.h"
#include "los_sec_pri.h"
#include "los_box_pri.h"
#include "los_task_pri.h"
#include "los_interrupt.h"
#include "securec.h"
#ifdef LOSCFG_BASE_IPC_MUX
#include "los_mux_pri.h"     /* g_osAllMux / LosMuxCB (stub resolve path) */
#endif
#ifdef LOSCFG_BASE_IPC_SEM
#include "los_sem_pri.h"     /* g_osAllSem / LosSemCB (stub resolve path) */
#endif
#ifdef LOSCFG_BASE_IPC_QUEUE
#include "los_queue.h"      /* LosQueueCB (kernel pool g_allQueue is declared
                             * below — OHOS keeps it in los_queue.c with no
                             * private header) */
extern LosQueueCB *g_allQueue;
#endif

#ifdef LOSCFG_BOX

/* kernel SID pool: static backing array (see los_sec_pri.h note). */
LITE_OS_SEC_BSS STATIC SIdObj g_kernelSIdPoolArr[LOSCFG_KERNEL_SID_LIMIT];
LITE_OS_SEC_BSS STATIC OS_SL_LIST g_kernelSIdFreeList;
SIdObj *g_kernelAllSId = NULL;

/* per-box SID free lists (see los_sec_pri.h note). */
LITE_OS_SEC_BSS OS_SL_LIST g_boxSIdFreeList[LOSCFG_BASE_BOX_MAX_LIMIT];

/* module registry: entity arrays per (type, boxId); kernel resources use
 * the LOS_KERNEL_SID_HEAD slot. */
LITE_OS_SEC_BSS STATIC SIdModule g_kernelSIdMods[LOS_SID_MOD_NUM];
LITE_OS_SEC_BSS STATIC SIdModule g_boxSIdMods[LOS_SID_MOD_NUM][LOSCFG_BASE_BOX_MAX_LIMIT];

/* ---- registry ---- */

SIdModule *OsSIdModListGet(UINT32 type, UINT16 boxId)
{
    if (type >= LOS_SID_MOD_NUM) {
        return NULL;
    }
    if (boxId == (UINT16)LOS_KERNEL_SID_HEAD) {
        return &g_kernelSIdMods[type];
    }
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return NULL;
    }
    return &g_boxSIdMods[type][boxId];
}

UINT32 OsSIdModuleInit(UINT32 moduleType, UINT8 permType,
                       EntityObj *entityList, UINT32 listLen, UINT16 boxId)
{
    SIdModule *mod = NULL;
    UINT32 idx;

    (VOID)permType;   /* LOSCFG_BASE_IPC_PERM off */

    if ((entityList == NULL) || (listLen == 0) || (moduleType >= LOS_SID_MOD_NUM)) {
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;
    }
    mod = OsSIdModListGet(moduleType, boxId);
    if (mod == NULL) {
        return LOS_ERRNO_SEC_INVALID_BOXID;
    }
    (VOID)memset(entityList, 0, sizeof(EntityObj) * listLen);
    for (idx = 0; idx < listLen; idx++) {
        entityList[idx].owner = boxId;
    }
    mod->moduleEntityArr = entityList;
    mod->listSize = listLen;
    return LOS_OK;
}

/* ---- pool init ---- */

VOID OsSIdListInit(VOID)
{
    UINT32 idx;

    g_kernelAllSId = g_kernelSIdPoolArr;
    OsSinglyListInit(&g_kernelSIdFreeList);
    (VOID)memset(g_kernelSIdPoolArr, 0, sizeof(g_kernelSIdPoolArr));
    for (idx = 0; idx < LOSCFG_KERNEL_SID_LIMIT; idx++) {
        OsSinglyListAdd(&g_kernelSIdFreeList, &g_kernelSIdPoolArr[idx].ipcList);
    }
}

UINT32 OsSIdBoxInit(LOS_DL_LIST *sidOwnerList, LOS_DL_LIST *sidFreeList,
                    SIdObj *sIdArr, UINT32 sIdSize)
{
    UINT16 boxId;
    UINT32 idx;

    /* reduced config: the per-box owner/free lists live in this module
     * (refs passes NULL/NULL without LOSCFG_BASE_SID_LIST) */
    (VOID)sidOwnerList;
    (VOID)sidFreeList;

    if ((sIdArr == NULL) || (sIdSize == 0)) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    for (boxId = 0; boxId < (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT; boxId++) {
        if (g_boxCBArr[boxId].boxSIdPool == sIdArr) {
            break;
        }
    }
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_ERRNO_SEC_INVALID_BOXID;
    }
    OsSinglyListInit(&g_boxSIdFreeList[boxId]);
    (VOID)memset(sIdArr, 0, sizeof(SIdObj) * sIdSize);
    for (idx = 0; idx < sIdSize; idx++) {
        OsSinglyListAdd(&g_boxSIdFreeList[boxId], &sIdArr[idx].ipcList);
    }
    return LOS_OK;
}

/* ---- validation (lock-free) ---- */

UINT32 OsSIdValidate(UINT32 sId)
{
    UINT16 boxId = OS_GET_SID_BOXID(sId);

    if (OS_GET_SID_TYPE(sId) >= LOS_SID_MOD_NUM) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    if ((boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) && (boxId != (UINT16)LOS_KERNEL_SID_HEAD)) {
        return LOS_ERRNO_SEC_INVALID_BOXID;
    }
    return LOS_OK;
}

UINT32 OsSIdOwnerCheck(UINT32 sId)
{
    /* STUB: cross-box owner checks are parked with the SIdObj pools
     * (kernel-head resources stay usable by every box — the box-heap
     * lock mux pends from box context). */
    (VOID)sId;
    return LOS_OK;
}

/* kernel-owned resources may be USED by boxes (pend/post the box heap
 * lock) but not CLAIMED/DESTROYED by them (refs defensive position in
 * LOS_QueueCreateWithSId). */
UINT32 OsSIdKernelResIntCheck(UINT32 sId)
{
    if (OsSIdValidate(sId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    if (OS_SID_BID_IS_KER(sId) && (OsGetCurBoxCB() != NULL)) {
        return LOS_ERRNO_SEC_PERM_DENIED;
    }
    return LOS_OK;
}

UINT32 OsSIdOwnerAndTypeCheck(UINT32 sId, UINT32 moduleType)
{
    if (OsSIdValidate(sId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    if (OS_GET_SID_TYPE(sId) != moduleType) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    return OsSIdOwnerCheck(sId);
}

UINT32 OsEntityOwnerCheck(const EntityObj *entityObj)
{
    if ((entityObj == NULL) || (entityObj->owner >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return LOS_ERRNO_SEC_INVALID_BOXID;
    }
    return LOS_OK;
}

UINT32 OsEntityOwnerCheckbySId(UINT32 sId)
{
    /* STUB: no entity binding behind the flat handle. */
    (VOID)sId;
    return LOS_OK;
}

UINT32 OsEntityOwnerGetBySid(UINT32 sId)
{
    /* STUB: no entity binding behind the flat handle. */
    (VOID)sId;
    return OS_INVALID;
}

/* ---- object lifecycle (lock-free; callers hold the scheduler lock) ---- */

/* take a free SIdObj from the current box pool (kernel task: kernel pool)
 * and stamp the unbound sId form */
STATIC UINT32 OsSIdObjAlloc(UINT32 type, SIdObj **outSIdObj)
{
    OS_SL_LIST *freeList = NULL;
    SIdObj *poolBase = NULL;
    SIdObj *sIdObj = NULL;
    UINT32 poolIdx;
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    UINT16 boxId;

    if (boxCB == NULL) {
        boxId = (UINT16)LOS_KERNEL_SID_HEAD;
        poolBase = g_kernelAllSId;
        freeList = &g_kernelSIdFreeList;
    } else {
        boxId = boxCB->boxId;
        poolBase = boxCB->boxSIdPool;
        freeList = &g_boxSIdFreeList[boxId];
    }
    if ((poolBase == NULL) || OsSinglyListEmpty(freeList)) {
        return LOS_ERRNO_SEC_SIDLIST_FULL;
    }
    sIdObj = OS_SL_LIST_ENTRY(OS_SL_LIST_FIRST(freeList), SIdObj, ipcList);
    OsSinglyListDelete(&sIdObj->ipcList);
    /* the low 10 bits carry the object's own slot index, so OsSIdObjGet
     * can decode the pool slot back from the sId */
    poolIdx = (UINT32)(sIdObj - poolBase);
    sIdObj->sId = OS_AUTO_SID_BIT | (((UINT32)boxId) << OS_SID_BOX_SHIFT) |
                  (type << OS_SID_TYPE_SHIFT) | poolIdx;
    sIdObj->entity = NULL;
    *outSIdObj = sIdObj;
    return LOS_OK;
}

/* return an SIdObj to its pool free list (derive the pool from the boxId
 * carried in the sId BEFORE wiping it). PARKED: the stub OsSIdGenObj no
 * longer allocates pool objects, so nothing calls this today — kept for
 * the real SID module (used: survive -Wunused-function). */
STATIC VOID __attribute__((used)) OsSIdObjFree(SIdObj *sIdObj)
{
    OS_SL_LIST *freeList = NULL;
    UINT16 boxId = OS_GET_SID_BOXID(sIdObj->sId);

    if (boxId == (UINT16)LOS_KERNEL_SID_HEAD) {
        freeList = &g_kernelSIdFreeList;
    } else if (boxId < (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        freeList = &g_boxSIdFreeList[boxId];
    } else {
        return;   /* corrupt box id: leak rather than corrupt a list */
    }
    sIdObj->sId = 0;
    sIdObj->entity = NULL;
    OsSinglyListAdd(freeList, &sIdObj->ipcList);
}

UINT32 OsSIdObjGet(UINT32 sId, SIdObj **outSIdObj)
{
    SIdObj *pool = NULL;
    UINT32 poolSize;
    UINT32 idx;

    if (OsSIdValidate(sId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    if (OS_SID_BID_IS_KER(sId)) {
        pool = g_kernelAllSId;
        poolSize = LOSCFG_KERNEL_SID_LIMIT;
    } else {
        const LosBoxCB *boxCB = &g_boxCBArr[OS_GET_SID_BOXID(sId)];
        pool = boxCB->boxSIdPool;
        poolSize = boxCB->boxSIdPoolSize;
    }
    idx = OS_GET_SID_POOLIDX(sId);
    if ((pool == NULL) || (idx >= poolSize) || (pool[idx].sId != sId)) {
        return LOS_ERRNO_SEC_INVALID_SID;   /* stale / forged sId */
    }
    *outSIdObj = &pool[idx];
    return LOS_OK;
}

UINT32 OsSIdBind(UINT32 type, SIdObj *sIdObj, UINT32 dataCBIdx, VOID *dataCB)
{
    SIdModule *mod = NULL;
    EntityObj *entity = NULL;
    UINT16 boxId;

    if ((sIdObj == NULL) || (sIdObj->entity != NULL)) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    boxId = OS_GET_SID_BOXID(sIdObj->sId);
    mod = OsSIdModListGet(type, boxId);
    if ((mod == NULL) || (mod->moduleEntityArr == NULL) || (dataCBIdx >= mod->listSize)) {
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;
    }
    entity = &mod->moduleEntityArr[dataCBIdx];
    if (entity->moduleData != NULL) {
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;   /* CB slot busy */
    }
    entity->owner = boxId;
    entity->moduleData = dataCB;
    sIdObj->entity = entity;
    sIdObj->sId |= (dataCBIdx << OS_SID_ARRAY_SHIFT);
    return LOS_OK;
}

UINT32 OsSIdGenObj(UINT32 type, UINT32 dataCBIdx, VOID *dataCB, UINT32 *sId)
{
#ifdef LOSCFG_BASE_IPC_MUX
    if (type == (UINT32)LOS_SID_TYPE_MUX) {
        /* STUB: flat handle — auto | boxId | type | idx. The owning pool
         * is inferred from the CB address: kernel mux-table CBs carry the
         * LOS_KERNEL_SID_HEAD box id (the kernel LOS_MuxCreate path —
         * pool locks created from box context), box-pool CBs carry their
         * box (the OsMuxCreate path). No SIdObj is allocated and no
         * module entity is bound. */
        UINT16 boxId;
        UINT32 b;

        boxId = (UINT16)LOS_KERNEL_SID_HEAD;
        if ((dataCB < (VOID *)g_osAllMux) ||
            (dataCB >= (VOID *)(g_osAllMux + LOSCFG_BASE_IPC_MUX_LIMIT))) {
            for (b = 0; b < LOSCFG_BASE_BOX_MAX_LIMIT; b++) {
                LosMuxCB *muxBase = (LosMuxCB *)g_boxCBArr[b].boxResMuxList;

                if ((muxBase != NULL) && (dataCB >= (VOID *)muxBase) &&
                    (dataCB < (VOID *)(muxBase + g_boxCBArr[b].boxResMuxNum))) {
                    boxId = (UINT16)b;
                    break;
                }
            }
            if (b >= LOSCFG_BASE_BOX_MAX_LIMIT) {
                return LOS_ERRNO_SEC_INVALID_SIDMODULE;   /* CB outside every pool */
            }
        }
        *sId = OS_AUTO_SID_BIT | (((UINT32)boxId) << OS_SID_BOX_SHIFT) |
               (((UINT32)type) << OS_SID_TYPE_SHIFT) | dataCBIdx;
        return LOS_OK;
    }
#else
    (VOID)dataCB;
#endif
#ifdef LOSCFG_BASE_IPC_SEM
    if (type == (UINT32)LOS_SID_TYPE_SEM) {
        /* STUB (M5E): same two-bound flat shape as MUX — kernel sem table
         * g_osAllSem vs the per-box sem pool (boxResSemList). The safe_ipc
         * port replaces this arm with the refs SIdObj-pool bind. */
        UINT16 boxId;
        UINT32 b;

        boxId = (UINT16)LOS_KERNEL_SID_HEAD;
        if ((dataCB < (VOID *)g_osAllSem) ||
            (dataCB >= (VOID *)(g_osAllSem + LOSCFG_BASE_IPC_SEM_LIMIT))) {
            for (b = 0; b < LOSCFG_BASE_BOX_MAX_LIMIT; b++) {
                LosSemCB *semBase = (LosSemCB *)g_boxCBArr[b].boxResSemList;

                if ((semBase != NULL) && (dataCB >= (VOID *)semBase) &&
                    (dataCB < (VOID *)(semBase + g_boxCBArr[b].boxResSemNum))) {
                    boxId = (UINT16)b;
                    break;
                }
            }
            if (b >= LOSCFG_BASE_BOX_MAX_LIMIT) {
                return LOS_ERRNO_SEC_INVALID_SIDMODULE;
            }
        }
        *sId = OS_AUTO_SID_BIT | (((UINT32)boxId) << OS_SID_BOX_SHIFT) |
               (((UINT32)type) << OS_SID_TYPE_SHIFT) | dataCBIdx;
        return LOS_OK;
    }
#else
    (VOID)dataCB;
#endif
#ifdef LOSCFG_BASE_IPC_QUEUE
    if (type == (UINT32)LOS_SID_TYPE_QUE) {
        /* STUB (M5E): kernel queue pool g_allQueue (OHOS keeps it in
         * los_queue.c, declared below) vs the per-box queue pool. */
        UINT16 boxId;
        UINT32 b;

        boxId = (UINT16)LOS_KERNEL_SID_HEAD;
        if ((dataCB < (VOID *)g_allQueue) ||
            (dataCB >= (VOID *)(g_allQueue + LOSCFG_BASE_IPC_QUEUE_LIMIT))) {
            for (b = 0; b < LOSCFG_BASE_BOX_MAX_LIMIT; b++) {
                LosQueueCB *queBase = (LosQueueCB *)g_boxCBArr[b].boxResQueList;

                if ((queBase != NULL) && (dataCB >= (VOID *)queBase) &&
                    (dataCB < (VOID *)(queBase + g_boxCBArr[b].boxResQueNum))) {
                    boxId = (UINT16)b;
                    break;
                }
            }
            if (b >= LOSCFG_BASE_BOX_MAX_LIMIT) {
                return LOS_ERRNO_SEC_INVALID_SIDMODULE;
            }
        }
        *sId = OS_AUTO_SID_BIT | (((UINT32)boxId) << OS_SID_BOX_SHIFT) |
               (((UINT32)type) << OS_SID_TYPE_SHIFT) | dataCBIdx;
        return LOS_OK;
    }
#else
    (VOID)dataCB;
#endif
    if (type == (UINT32)LOS_SID_TYPE_EVT) {
        /* STUB (M5E): framework-only. The OHOS baseline has no kernel
         * event pool (event CBs are user-embedded) and the refs safe_ipc
         * pools (g_osAllEvent / LosEventCB / per-box free lists) do not
         * exist yet — stamp the kernel head unconditionally; the safe_ipc
         * port tightens the bounds when its pools land. */
        *sId = OS_AUTO_SID_BIT | (((UINT32)LOS_KERNEL_SID_HEAD) << OS_SID_BOX_SHIFT) |
               (((UINT32)type) << OS_SID_TYPE_SHIFT) | dataCBIdx;
        return LOS_OK;
    }
    (VOID)dataCBIdx;
    if (sId == NULL) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    return LOS_ERRNO_SEC_INVALID_SIDMODULE;   /* MUX/SEM/QUE flat-wired; EVT framework-only (M5E) */
}

UINT32 OsSIdObjDel(UINT32 sId)
{
    /* STUB: nothing is bound behind the flat handle; the mux CB recycle
     * runs in OsMuxNodeRecycleBySId, driven by the same sId bits. */
    (VOID)sId;
    return LOS_OK;
}

UINT32 OsSIdGetDataCB(UINT32 sId, UINT32 moduleType, VOID **dataCB)
{
    /* STUB: resolve the CB straight from the flat handle — the kernel
     * table for kernel-head ids, the per-box pool otherwise. MUX/SEM/QUE
     * are flat-wired (M5E); EVT has no pool to index yet and stays
     * framework-only until the safe_ipc event pools land. */
    UINT32 idx = sId & ((1U << OS_SID_ARRAY_SHIFT) - 1U);
    UINT16 boxId = OS_GET_SID_BOXID(sId);

    if (dataCB == NULL) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    if ((sId & OS_AUTO_SID_BIT) == 0) {
        return LOS_ERRNO_SEC_INVALID_SID;   /* not a generated handle (e.g. the
                                             * memMux==0 rollback residue) */
    }
    if (OsSIdValidate(sId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
#ifdef LOSCFG_BASE_IPC_MUX
    if (moduleType == (UINT32)LOS_SID_TYPE_MUX) {
        if (OS_GET_SID_TYPE(sId) != (UINT32)LOS_SID_TYPE_MUX) {
            return LOS_ERRNO_SEC_INVALID_SID;
        }
        if (boxId == (UINT16)LOS_KERNEL_SID_HEAD) {
            if (idx >= (UINT32)LOSCFG_BASE_IPC_MUX_LIMIT) {
                return LOS_ERRNO_SEC_INVALID_SID;
            }
            *dataCB = (VOID *)&g_osAllMux[idx];
            return LOS_OK;
        }
        {
            const LosBoxCB *boxCB = &g_boxCBArr[boxId];

            if ((boxCB->boxResMuxList == NULL) || (idx >= boxCB->boxResMuxNum)) {
                return LOS_ERRNO_SEC_INVALID_SID;
            }
            *dataCB = (VOID *)&((LosMuxCB *)boxCB->boxResMuxList)[idx];
        }
        return LOS_OK;
    }
#else
    (VOID)idx;
    (VOID)boxId;
#endif
#ifdef LOSCFG_BASE_IPC_SEM
    if (moduleType == (UINT32)LOS_SID_TYPE_SEM) {
        if (OS_GET_SID_TYPE(sId) != (UINT32)LOS_SID_TYPE_SEM) {
            return LOS_ERRNO_SEC_INVALID_SID;
        }
        if (boxId == (UINT16)LOS_KERNEL_SID_HEAD) {
            if (idx >= (UINT32)LOSCFG_BASE_IPC_SEM_LIMIT) {
                return LOS_ERRNO_SEC_INVALID_SID;
            }
            *dataCB = (VOID *)&g_osAllSem[idx];
            return LOS_OK;
        }
        {
            const LosBoxCB *boxCB = &g_boxCBArr[boxId];

            if ((boxCB->boxResSemList == NULL) || (idx >= boxCB->boxResSemNum)) {
                return LOS_ERRNO_SEC_INVALID_SID;
            }
            *dataCB = (VOID *)&((LosSemCB *)boxCB->boxResSemList)[idx];
        }
        return LOS_OK;
    }
#endif
#ifdef LOSCFG_BASE_IPC_QUEUE
    if (moduleType == (UINT32)LOS_SID_TYPE_QUE) {
        if (OS_GET_SID_TYPE(sId) != (UINT32)LOS_SID_TYPE_QUE) {
            return LOS_ERRNO_SEC_INVALID_SID;
        }
        if (boxId == (UINT16)LOS_KERNEL_SID_HEAD) {
            if ((g_allQueue == NULL) || (idx >= (UINT32)LOSCFG_BASE_IPC_QUEUE_LIMIT)) {
                return LOS_ERRNO_SEC_INVALID_SID;
            }
            *dataCB = (VOID *)&g_allQueue[idx];
            return LOS_OK;
        }
        {
            const LosBoxCB *boxCB = &g_boxCBArr[boxId];

            if ((boxCB->boxResQueList == NULL) || (idx >= boxCB->boxResQueNum)) {
                return LOS_ERRNO_SEC_INVALID_SID;
            }
            *dataCB = (VOID *)&((LosQueueCB *)boxCB->boxResQueList)[idx];
        }
        return LOS_OK;
    }
#endif
    if (moduleType == (UINT32)LOS_SID_TYPE_EVT) {
        /* STUB (M5E): no pool to index — the kernel event pool does not
         * exist in the OHOS baseline and the refs safe_ipc pools
         * (g_osAllEvent / per-box) are not ported yet. Framework-only. */
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    return LOS_ERRNO_SEC_INVALID_SIDMODULE;
}

VOID OsSIdRecycle(const EntityObj *entityObj, VOID *recoverCB)
{
    /* refs unbinds every SIdObj bound to this entity onto the recover
     * list; without IPC_RECOVER there is no recover list - the box
     * destroy path frees SIdObjs via OsSIdObjDel. */
    (VOID)entityObj;
    (VOID)recoverCB;
}

/* ---- public API (self-locking wrappers) ---- */

UINT32 LOS_GenSId(UINT32 type)
{
    UINT32 intSave;
    UINT32 ret;
    SIdObj *sIdObj = NULL;

    if (type >= LOS_SID_MOD_NUM) {
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;
    }
    SCHEDULER_LOCK(intSave);
    ret = OsSIdObjAlloc(type, &sIdObj);
    SCHEDULER_UNLOCK(intSave);
    if (ret != LOS_OK) {
        return ret;
    }
    return sIdObj->sId;
}

UINT32 LOS_SIdDelete(UINT32 sId)
{
    UINT32 intSave;
    UINT32 ret;

    SCHEDULER_LOCK(intSave);
    ret = OsSIdObjDel(sId);
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

#endif /* LOSCFG_BOX */

/*
 * los_sec.c — SID (system identifier) module core.
 *
 * Implements the contracts in los_sec_pri.h under the reduced config (no
 * IPC_PERM / IPC_RECOVER / BASE_SID_LIST / EXC_ISOLATION). The shape is refs
 * kernel_s/base/los_sec.c with those four blocks compiled out:
 *
 *  - sId layout: see los_sec_pri.h
 *    (auto:1 | boxId:7 | type:4 | poolSlot:10 | salt:10).
 *
 *  - resolution goes through the middle layer: sId -> SIdObj slot (in the
 *    owning box's pool, or the kernel pool) -> EntityObj -> moduleData (the
 *    real CB). The data-CB index never appears in the sId, so a resolve is
 *    two indexed loads and needs no per-module range arithmetic.
 *
 *  - locking: SID primitives are lock-free; every caller holds the
 *    scheduler lock around a Gen/Bind/Get/Del sequence (OsSIdObjGet +
 *    OsSIdBind inside SCHEDULER_LOCK). The public LOS_GenSId /
 *    LOS_SIdDelete wrappers take the lock themselves.
 *
 * OWNER SELECTION - the one place this file cannot follow refs literally.
 * refs' OsSIdCreate takes the owner from OsGetCurBoxCB(), which is correct
 * there because refs keeps the plain and the SID mux APIs apart: its
 * MemLockCreate reaches the USR-world LOS_MuxCreate (kernel_s/usr/
 * los_usr_api.c), so a box heap lock is drawn from the box pool and the
 * context always agrees with the CB. This tree merges the two into one
 * LOS_MuxCreate, and its box heap lock is created the same way - but the
 * earlier note here claiming that made a pool-backed form impossible was
 * WRONG: under BOX the allocator is compiled once in the usr world
 * (box_usr_mem), so MemLockCreate's LOS_MuxCreate resolves to the usr
 * wrapper -> OsMuxCreate -> the box pool, and OsBoxEnvBuild deliberately
 * does not touch the box heap at all. Context and CB pool do agree.
 * So OsSIdCreate below is refs-verbatim.
 *
 * All four module types are two-bounded (kernel table + per-box pool) and all
 * four register an entity array for each bound: MUX g_osAllMux/
 * boxResMuxList, SEM g_osAllSem/boxResSemList, QUE g_allQueue/
 * boxResQueList, EVT g_osAllEvent/boxResEvtList.
 *
 * EVT note: events have TWO access families and only one of them comes here.
 * The pointer family (LOS_EventInit / LOS_EventRead over a caller-owned
 * EVENT_CB_S, including the one embedded in each TaskCB) never touches the
 * SID module. The id family (LOS_EventCreate -> sId, LOS_Event*WithSId) does,
 * and it needs both bounds: a kernel-created event must be handable to a box
 * as an sId, which is what the kernel table is for.
 */
#include "los_typedef.h"
#include "los_sec_pri.h"
#include "los_box_pri.h"
#include "los_task_pri.h"
#include "los_interrupt.h"
#include "securec.h"
/* No per-module private headers here: resolution is module-agnostic now (the
 * entity pointer carries the CB), so this TU no longer names LosMuxCB /
 * LosSemCB / LosEventCB or any of the pool globals. The one exception is the
 * queue create exit below, which reaches into LosQueueCB fields. */
#ifdef LOSCFG_BASE_IPC_QUEUE
#include "los_memory.h"     /* LOS_MemFree (OsQueueSecCreateOut reclaim) */
#include "los_queue.h"      /* LosQueueCB (OsQueueSecCreateOut field accesses) */
#endif

#ifdef LOSCFG_BOX

/* kernel SID pool: static backing array (see los_sec_pri.h note). The
 * pointer is published for los_sec_pri.h's extern and initialised statically,
 * the same pattern los_mux.c uses for g_kernelMuxEntity. */
LITE_OS_SEC_BSS STATIC SIdObj g_kernelSIdPoolArr[LOSCFG_KERNEL_SID_LIMIT];
SIdObj *g_kernelAllSId = g_kernelSIdPoolArr;

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
        /* refs form: seed the alias-chain head. owner is NOT stamped here -
         * OsSIdBind sets it when a CB actually binds to the slot. */
        OsSinglyListInit(&entityList[idx].userSIdList);
    }
    mod->moduleEntityArr = entityList;
    mod->listSize = listLen;
    return LOS_OK;
}

/* ---- pool init ---- */

/* refs OsSIdListInit with the IPC_RECOVER and BASE_SID_LIST blocks compiled
 * out; what is left is refs' OS_SID_KERNEL_INIT(g_kernelAllSId, limit), i.e.
 * a plain OsSIdBoxInit over the kernel pool. Called from OsIpcInit
 * (los_init.c) BEFORE any module init: the pool is shared by every IPC
 * module, not owned by one. */
VOID OsSIdListInit(VOID)
{
    (VOID)OsSIdBoxInit(NULL, NULL, g_kernelAllSId, LOSCFG_KERNEL_SID_LIMIT);
}

/* refs OsSIdBoxInit, BASE_SID_LIST branch: with no owner/free lists to build,
 * resetting a pool is just zeroing it - a slot is free exactly when its sId
 * is 0, which is what OsSIdGetFreeSIdObj scans for. Also the box-restart
 * reset path (los_box.c re-runs this over boxSIdPool). */
UINT32 OsSIdBoxInit(LOS_DL_LIST *sidOwnerList, LOS_DL_LIST *sidFreeList,
                    SIdObj *sIdArr, UINT32 sIdSize)
{
    (VOID)sidOwnerList;
    (VOID)sidFreeList;

    if ((sIdArr == NULL) || (sIdSize == 0)) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    (VOID)memset(sIdArr, 0, sizeof(SIdObj) * sIdSize);
    return LOS_OK;
}

/* ---- validation (lock-free) ---- */

/* refs OsSIdValidate; refs' KERNEL_BOX_LIMIT is spelled
 * LOSCFG_BASE_BOX_MAX_LIMIT here (same bound, this tree's name). */
UINT32 OsSIdValidate(UINT32 sId)
{
    UINT16 boxId = OS_GET_SID_BOXID(sId);

    if ((sId == 0) || (OS_GET_SID_TYPE(sId) >= LOS_SID_MOD_NUM)) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    if ((boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) && (boxId != (UINT16)LOS_KERNEL_SID_HEAD)) {
        return LOS_ERRNO_SEC_INVALID_BOXID;
    }
    return LOS_OK;
}

/* refs OsSIdOwnerCheck: outside an interrupt a box may only touch its own
 * resources. Note this REJECTS a kernel-head sId for a box caller, so it must
 * stay off the resolve path - the "boxes may USE kernel resources" allowance
 * is why OsSIdGetDataCB does not call it (refs agrees: its only caller there
 * is inside LOSCFG_BASE_IPC_PERM). Reachable only from the claim/delete paths
 * (OsSIdOwnerAndTypeCheck, LOS_SIdDelete), neither of which has a live caller
 * in this tree yet. */
UINT32 OsSIdOwnerCheck(UINT32 sId)
{
    UINT32 ret;
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    UINT32 ownerBoxId = OS_GET_SID_BOXID(sId);

    ret = OsSIdValidate(sId);
    if (ret != LOS_OK) {
        return ret;
    }

    if ((OS_INT_INACTIVE) && (boxCB != NULL) && (ownerBoxId != boxCB->boxId)) {
        return LOS_ERRNO_SEC_PERM_DENIED;
    }

    return LOS_OK;
}

/* kernel-owned resources may be USED by boxes (pend/post the box heap
 * lock) but not CLAIMED/DESTROYED by them - the same defensive position
 * LOS_QueueCreateWithSId takes. Renamed away from the refs spelling because
 * refs' OsSIdKernelResIntCheck (below) means something else entirely; see the
 * note in los_sec_pri.h. */
UINT32 OsSIdKernelResClaimCheck(UINT32 sId)
{
    if (OsSIdValidate(sId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    if (OS_SID_BID_IS_KER(sId) && (OsGetCurBoxCB() != NULL)) {
        return LOS_ERRNO_SEC_PERM_DENIED;
    }
    return LOS_OK;
}

/* refs OsSIdKernelResIntCheck, verbatim: an interrupt may only be handed a
 * kernel-head resource, because a box's pool state is not safe to touch from
 * an ISR. Called from OsCondVarCreate's pre-generated-sId arm (refs' shape);
 * this tree has no other pre-generated-sId create path. */
UINT32 OsSIdKernelResIntCheck(UINT32 sId)
{
    if (OS_INT_ACTIVE && (OS_GET_SID_BOXID(sId) != (UINT16)LOS_KERNEL_SID_HEAD)) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    return LOS_OK;
}

/* refs OsSIdOwnerAndTypeCheck: owner first, then the type field. */
UINT32 OsSIdOwnerAndTypeCheck(UINT32 sId, UINT32 moduleType)
{
    UINT32 ret = OsSIdOwnerCheck(sId);
    if (ret != LOS_OK) {
        return ret;
    }
    if (moduleType != OS_GET_SID_TYPE(sId)) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    return LOS_OK;
}

/* refs OsEntityOwnerCheck. Note the owner of a KERNEL entity is
 * LOS_KERNEL_SID_HEAD (0x7F), which is outside the box range - so this check
 * is deliberately about "same box", not "valid box": it only fires for a box
 * caller, and a box caller may not claim a kernel entity either. */
UINT32 OsEntityOwnerCheck(const EntityObj *entityObj)
{
    const LosBoxCB *boxCB = OsGetCurBoxCB();

    if (entityObj == NULL) {
        return LOS_ERRNO_SEC_RESOURCE_NONE;
    }

    if ((OS_INT_INACTIVE) && (boxCB != NULL) &&
        (entityObj->owner != boxCB->boxId)) {
        return LOS_ERRNO_SEC_PERM_DENIED;
    }

    return LOS_OK;
}

/* refs OsEntityOwnerCheckbySId with the IPC_RECOVER flags test compiled out. */
UINT32 OsEntityOwnerCheckbySId(UINT32 sId)
{
    SIdObj *sIdObj = NULL;

    if (OsSIdObjGet(sId, &sIdObj) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    return OsEntityOwnerCheck(sIdObj->entity);
}

/* refs OsEntityOwnerGetBySid, made defensive: refs asserts the slot and the
 * entity are non-NULL and then dereferences both, so with asserts compiled out
 * a bad sId is a NULL read in kernel mode. Returning OS_INVALID instead is a
 * recorded deviation - the function has no caller in this tree, and a silent
 * NULL deref is worse than a short check. */
UINT32 OsEntityOwnerGetBySid(UINT32 sId)
{
    SIdObj *sIdObj = NULL;

    if (OsSIdObjGet(sId, &sIdObj) != LOS_OK) {
        return OS_INVALID;
    }
    if (sIdObj->entity == NULL) {
        return OS_INVALID;
    }
    return sIdObj->entity->owner;
}

/* ---- object lifecycle (lock-free; callers hold the scheduler lock) ---- */

/* refs OsSIdGetAutoIndex, LOSCFG_RAND_SID branch off: a monotonic counter
 * masked into the sId's low 10 bits, so a slot handed back out does not
 * reproduce the sId that may still be circulating from its previous life. */
STATIC UINT32 OsSIdGetAutoIndex(VOID)
{
    STATIC UINT32 sIdAuto = 0;
    sIdAuto = ~(OS_AUTO_SID_BIT | OS_SID_BOXID_MASK | OS_SID_TYPE_MASK |
                OS_SID_ARRAY_MASK) & (sIdAuto + (UINT32)1);
    return sIdAuto;
}

/* refs OsSIdGetFreeSIdObj, BASE_SID_LIST branch: there is no free list, so a
 * free slot is one whose sId is still 0 (the state OsSIdClear restores) and
 * the pool is scanned for it. *sIdArrIdx comes back already shifted into the
 * OS_SID_ARRAY_MASK field. */
STATIC SIdObj *OsSIdGetFreeSIdObj(UINT32 sIdBox, UINT32 *sIdArrIdx)
{
    SIdObj *sIdObj = NULL;
    const SIdObj *sIdPool = NULL;
    UINT32 sIdPoolSize;
    UINT32 i;

    if (sIdBox == (UINT32)LOS_KERNEL_SID_HEAD) {
        sIdPoolSize = LOSCFG_KERNEL_SID_LIMIT;
        sIdPool = g_kernelAllSId;
    } else {
        sIdPoolSize = g_boxCBArr[sIdBox].boxSIdPoolSize;
        sIdPool = g_boxCBArr[sIdBox].boxSIdPool;
    }
    if (sIdPool == NULL) {
        return NULL;
    }

    for (i = 0; i < sIdPoolSize; i++) {
        if (sIdPool[i].sId == 0) {
            sIdObj = (SIdObj *)&sIdPool[i];
            break;
        }
    }

    if (sIdObj == NULL) {
        return NULL;
    }

    (VOID)memset((VOID *)sIdObj, 0, sizeof(SIdObj));

    *sIdArrIdx = ((UINTPTR)sIdObj - (UINTPTR)sIdPool) / sizeof(SIdObj);
    *sIdArrIdx = (*sIdArrIdx << OS_SID_ARRAY_SHIFT) & OS_SID_ARRAY_MASK;

    return sIdObj;
}

/* refs OsSIdCreate: the owner is the CURRENT context's box, or the kernel head
 * for a kernel caller. See the file header for why that is safe here - the box
 * heap lock is drawn from the box pool, so the context always agrees with the
 * CB's pool and the entity array indexed at bind time is the right one. */
STATIC SIdObj *OsSIdCreate(UINT32 type)
{
    UINT32 sIdBox, sIdType;
    SIdObj *sIdObj = NULL;
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    UINT32 sIdArrIdx;

    if (boxCB == NULL) {
        sIdBox = (UINT32)LOS_KERNEL_SID_HEAD;
    } else {
        sIdBox = boxCB->boxId;
    }

    sIdObj = OsSIdGetFreeSIdObj(sIdBox, &sIdArrIdx);
    if (sIdObj == NULL) {
        return NULL;
    }

    sIdBox  = (sIdBox << OS_SID_BOX_SHIFT) & OS_SID_BOXID_MASK;
    sIdType = (type << OS_SID_TYPE_SHIFT) & OS_SID_TYPE_MASK;

    sIdObj->sId = OS_AUTO_SID_BIT | sIdBox | sIdType | sIdArrIdx | OsSIdGetAutoIndex();

    return sIdObj;
}

/* refs OsSIdClear, BASE_SID_LIST branch: zeroing the slot IS the return to the
 * pool, because sId == 0 is the free marker. */
STATIC INLINE VOID OsSIdClear(SIdObj *sIdObj)
{
    (VOID)memset(sIdObj, 0, sizeof(SIdObj));
}

/* refs OsSIdReset: drop the slot's link in its entity's alias chain first,
 * then clear it. */
STATIC VOID OsSIdReset(SIdObj *sIdObj)
{
    if (OS_SL_LIST_FIRST(&sIdObj->ipcList) != NULL) {
        OsSinglyListDelete(&sIdObj->ipcList);
    }

    OsSIdClear(sIdObj);
}

/* refs OsSIdObjGet: decode the pool-slot field, then require the slot to still
 * carry exactly this sId - that comparison is what makes a stale or forged
 * handle fail instead of resolving to a recycled CB. The box arm also refuses a
 * box that is not up. OsSIdValidate is kept in front (refs relies on its
 * callers) because OsEntityOwnerCheckbySId / OsSIdObjDel reach this directly,
 * and without it an out-of-range boxId would index g_boxCBArr out of bounds. */
UINT32 OsSIdObjGet(UINT32 sId, SIdObj **outSIdObj)
{
    UINT32 boxId;
    UINT32 sIdArr, sIdPoolSize;
    const SIdObj *sIdPool = NULL;

    if (OsSIdValidate(sId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }

    boxId = OS_GET_SID_BOXID(sId);
    if (boxId == (UINT32)LOS_KERNEL_SID_HEAD) {
        sIdPool = g_kernelAllSId;
        sIdPoolSize = LOSCFG_KERNEL_SID_LIMIT;
    } else {
        if (!BOX_IS_INIT(g_boxCBArr[boxId].boxStatus)) {
            return LOS_ERRNO_SEC_INVALID_BOXID;
        }
        sIdPool = g_boxCBArr[boxId].boxSIdPool;
        sIdPoolSize = g_boxCBArr[boxId].boxSIdPoolSize;
    }

    sIdArr = OS_GET_SID_ARRIDX(sId);
    if ((sIdPool == NULL) || (sIdArr >= sIdPoolSize) || (sIdPool[sIdArr].sId != sId)) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }

    *outSIdObj = (SIdObj *)&sIdPool[sIdArr];
    return LOS_OK;
}

/* refs OsSIdBind, keeping two checks this tree already had: dataCBIdx against
 * the module's listSize (refs indexes the entity array unchecked) and the
 * "slot already busy" rejection. Both turn a silent out-of-range write or a
 * silent rebind into an error - recorded as deliberate hardening. */
UINT32 OsSIdBind(UINT32 type, SIdObj *sIdObj, UINT32 dataCBIdx, VOID *dataCB)
{
    UINT16 boxId;
    EntityObj *entityArr = NULL;
    const SIdModule *module = NULL;

    if ((dataCB == NULL) || (sIdObj == NULL) || (sIdObj->entity != NULL)) {
        return LOS_NOK;
    }

    boxId = OS_GET_SID_BOXID(sIdObj->sId);
    module = OsSIdModListGet(type, boxId);
    if ((module == NULL) || (module->moduleEntityArr == NULL) ||
        (dataCBIdx >= module->listSize)) {
        return LOS_NOK;
    }

    entityArr = module->moduleEntityArr;
    if (entityArr[dataCBIdx].moduleData != NULL) {
        return LOS_NOK;   /* CB slot busy: the previous owner never unbound */
    }
    (VOID)memset(entityArr[dataCBIdx].regName, 0, sizeof(entityArr[dataCBIdx].regName));
    entityArr[dataCBIdx].owner = boxId;
    entityArr[dataCBIdx].moduleData = dataCB;
    sIdObj->entity = &entityArr[dataCBIdx];
    OsSinglyListAdd(&entityArr[dataCBIdx].userSIdList, &sIdObj->ipcList);

    return LOS_OK;
}

/* refs OsSIdGenObj: draw a slot for the current context, bind the CB's entity
 * to it, hand back the sId. The four per-module address-range arms that used to
 * live here are gone - the owner comes from the context (OsSIdCreate) and the
 * CB hangs off the entity, so generation needs no module-specific arithmetic.
 * Deviation: the two failure paths return distinct errnos (SIDLIST_FULL vs
 * INVALID_SIDMODULE) where refs returns LOS_NOK for both, matching what this
 * tree's OsSIdModuleInit already does; LOS_EventCreate propagates the code
 * straight to the caller, so the distinction is visible to box code. */
UINT32 OsSIdGenObj(UINT32 type, UINT32 dataCBIdx, VOID *dataCB, UINT32 *sId)
{
    SIdObj *newSId = NULL;

    if ((sId == NULL) || OS_INT_ACTIVE) {
        return LOS_NOK;
    }

    newSId = OsSIdCreate(type);
    if (newSId == NULL) {
        return LOS_ERRNO_SEC_SIDLIST_FULL;
    }

    if (OsSIdBind(type, newSId, dataCBIdx, dataCB) != LOS_OK) {
        OsSIdReset(newSId);
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;
    }

    *sId = newSId->sId;
    return LOS_OK;
}

/* refs OsSIdRecycleByDel with the EXC_ISOLATION / IPC_RECOVER variants
 * compiled out: deleting a handle always recycles the whole entity, so it
 * reports "current box resource" unconditionally. */
STATIC UINT32 OsSIdRecycleByDel(SIdObj *sIdObj, UINT16 boxId, const VOID *boxCB)
{
    const EntityObj *entityObj = sIdObj->entity;
    (VOID)boxId;
    (VOID)boxCB;

    OsSIdRecycle(entityObj, NULL);
    return IS_CURRENT_BOX_RES;
}

/* refs OsSIdObjDelDo, BASE_SID_LIST name-unlink compiled out.
 * refs' last line reads "OsSinglyListInit(&EntityObj->userSIdList)" - the TYPE
 * name where the variable was meant. The intent is both clear and necessary:
 * the memset above zeroes the chain head, and OsSinglyListEmpty tests
 * pstNext == &head, so a zeroed head would read as NON-empty and the next bind
 * to this entity would walk off a NULL. Spelled correctly here; recorded as a
 * refs typo fix (same class as M6I's OsMemPoolHead / OsBoxMulPoolHeadClear). */
STATIC VOID OsSIdObjDelDo(SIdObj *sIdObj, UINT16 boxId, const VOID *boxCB,
                          UINT32 (*recycleCb)(SIdObj *, UINT16, const VOID *))
{
    EntityObj *entityObj = NULL;

    if (sIdObj->entity == NULL) {
        OsSIdReset(sIdObj);
        return;
    }

    entityObj = sIdObj->entity;

    if ((recycleCb != NULL) && (recycleCb(sIdObj, boxId, boxCB) != IS_CURRENT_BOX_RES)) {
        return;
    }
    (VOID)memset(entityObj, 0, sizeof(EntityObj));
    OsSinglyListInit(&entityObj->userSIdList);
}

/* refs OsSIdObjDel: resolve the slot, then unbind the entity (recycling every
 * sId aliased to it) and free the slot. The caller holds the scheduler lock. */
UINT32 OsSIdObjDel(UINT32 sId)
{
    UINT32 ret;
    SIdObj *sIdObj = NULL;

    ret = OsSIdObjGet(sId, &sIdObj);
    if (ret != LOS_OK) {
        return ret;
    }

    OsSIdObjDelDo(sIdObj, OS_GET_SID_BOXID(sIdObj->sId), NULL, OsSIdRecycleByDel);
    return LOS_OK;
}

UINT32 OsSIdGetDataCB(UINT32 sId, UINT32 moduleType, VOID **dataCB)
{
    /* refs OsSIdGetDataCB with the IPC_RECOVER flag tests and the whole
     * IPC_PERM block compiled out: validate, match the type field, resolve the
     * slot, hand back the entity's CB. ONE path for all four module types -
     * the per-module two-bound arms that used to be here are gone, because the
     * entity pointer already records which pool the CB came from.
     *
     * Two deviations, both recorded:
     *  - the dataCB NULL check refs lacks (it writes *dataCB unconditionally);
     *  - refs probes the caller here (OsCurrTaskGet + curTaskCB->boxCB) but the
     *    probe only feeds the PERM block, and with PERM off both of its outcomes
     *    fall through to the same success path. Compiling it out with the block
     *    keeps a dead external call off every IPC entry. (refs reaches the box
     *    through curTaskCB->boxCB; this tree has the same field -
     *    los_task_base.h - but OsGetCurBoxCB() is what the rest of it uses.)
     *    The probe comes back with the IPC_PERM work (deferred).
     *
     * Stale handles now fail here rather than resolving to a recycled CB: the
     * slot's stored sId must match exactly (OsSIdObjGet), and a deleted
     * resource has no entity at all. */
    UINT32 ret;
    SIdObj *sIdObj = NULL;
    const EntityObj *entityObj = NULL;

    if (dataCB == NULL) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }

    ret = OsSIdValidate(sId);
    if (ret != LOS_OK) {
        return ret;
    }

    if (OS_GET_SID_TYPE(sId) != moduleType) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }

    if (OsSIdObjGet(sId, &sIdObj) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }

    if (sIdObj->entity == NULL) {
        return LOS_ERRNO_SEC_RESOURCE_NONE;
    }

    entityObj = sIdObj->entity;
    *dataCB = entityObj->moduleData;
    return LOS_OK;
}

/* OsIpcSecIdNorm - entry gate (self-locking):
 * normalize a SID handle into its pool index IN PLACE. idOffset is the
 * offset of the pool handle field inside the control block and idWidth its
 * size, so each module's OsXxxIdNorm inline gate passes its own field type
 * and the generic logic still lives in exactly one place. On failure
 * *handle is left untouched and the caller returns its own error code.
 *
 * The declared width matters: LosQueueCB.queueID is UINT16 at offset 14
 * (2-mod-4), so an unconditional 32-bit read of it is an unaligned access -
 * it traps (UFSR.UNALIGNED, escalated to HardFault). Reading through the
 * declared width keeps every access naturally aligned. */
UINT32 OsIpcSecIdNorm(UINT32 *handle, UINT32 sidType, UINT32 idOffset, UINT32 idWidth)
{
    VOID *dataCB = NULL;
    UINT32 intSave;
    UINT32 ret;

    if ((handle == NULL) || ((idWidth != sizeof(UINT16)) && (idWidth != sizeof(UINT32)))) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }

    SCHEDULER_LOCK(intSave);
    ret = OsSIdGetDataCB(*handle, sidType, &dataCB);
    if ((ret == LOS_OK) && (dataCB != NULL)) {
        const UINT8 *field = (const UINT8 *)dataCB + idOffset;
        *handle = (idWidth == sizeof(UINT16)) ? (UINT32)(*(const UINT16 *)(const VOID *)field)
                                              : *(const UINT32 *)(const VOID *)field;
    }
    SCHEDULER_UNLOCK(intSave);

    return ret;
}

#ifdef LOSCFG_BASE_IPC_QUEUE
/* OsQueueSecCreateOut - queue create exit
 * (self-locking): allocate and bind the SID; on failure reclaim the CB with
 * LOS_QueueDelete's lock discipline (field clear + free-list re-insert under
 * the lock, queue memory freed outside it per isStatic). cb is really a
 * LosQueueCB *, passed as VOID * so callers holding only a generic control
 * block need no queue header; this TU includes it for the field accesses. */
UINT32 OsQueueSecCreateOut(UINT32 *queueID, VOID *cb, VOID *queueMem,
                           LOS_DL_LIST *freeList, BOOL isStatic)
{
    LosQueueCB *queueCB = (LosQueueCB *)cb;
    UINT32 intSave;
    UINT32 ret;

    SCHEDULER_LOCK(intSave);
    ret = OsSIdGenObj((UINT32)LOS_SID_TYPE_QUE, queueCB->queueID, (VOID *)queueCB, queueID);
    if (ret != LOS_OK) {
        queueCB->queue = (UINT8 *)NULL;
        queueCB->queueName = (UINT8 *)NULL;
        queueCB->queueState = OS_QUEUE_UNUSED;
        LOS_ListTailInsert(freeList, &queueCB->readWriteList[OS_QUEUE_WRITE]);
    }
    SCHEDULER_UNLOCK(intSave);

    if ((ret != LOS_OK) && (isStatic == FALSE) && (queueMem != NULL)) {
        (VOID)LOS_MemFree(m_aucSysMem0, queueMem);
    }
    return ret;
}
#endif /* LOSCFG_BASE_IPC_QUEUE */

/* refs OsSIdRecycle with IPC_RECOVER compiled out: unbind EVERY sId aliased to
 * this entity and free its slot. With no recover list there is nowhere to park
 * a foreign-box alias, so all of them are simply dropped. Walks the entity's
 * userSIdList; OsSinglyListDelete unlinks the node itself and re-closes the
 * ring, so the head reads empty once the last alias is gone. */
VOID OsSIdRecycle(const EntityObj *entityObj, VOID *recoverCB)
{
    SIdObj *sIdObj = NULL;
    (VOID)recoverCB;

    while (!OsSinglyListEmpty(&entityObj->userSIdList)) {
        sIdObj = OS_SL_LIST_ENTRY(OS_SL_LIST_FIRST(&entityObj->userSIdList), SIdObj, ipcList);
        OsSinglyListDelete(&sIdObj->ipcList);
        sIdObj->entity = NULL;
        OsSIdReset(sIdObj);
    }
}

/* ---- public API (self-locking wrappers) ---- */

/* refs LOS_GenSId: hand out an UNBOUND sId of the given type, for a caller
 * that wants to pass it into a later create (the *sId != 0 arm of refs'
 * OsXxxCreate). No caller in this tree yet - the create paths all go through
 * OsSIdGenObj, which allocates and binds in one step.
 * Deviation: refs reads sIdObj->sId after SCHEDULER_UNLOCK; the slot is shared
 * state, so the read is moved inside the lock. */
UINT32 LOS_GenSId(UINT32 type)
{
    UINT32 intSave;
    UINT32 sId;
    SIdObj *sIdObj = NULL;

    if ((OS_INT_ACTIVE) || (type >= (UINT32)LOS_SID_MOD_NUM)) {
        return 0;
    }

    SCHEDULER_LOCK(intSave);
    sIdObj = OsSIdCreate(type);
    if (sIdObj == NULL) {
        SCHEDULER_UNLOCK(intSave);
        return 0;
    }
    sId = sIdObj->sId;
    SCHEDULER_UNLOCK(intSave);
    return sId;
}

/* refs LOS_SIdDelete: release a BARE sId slot - one that was generated but
 * never bound, or whose entity has already been recycled. A slot that still
 * has an entity (or is still linked in an alias chain) is rejected; tearing
 * down a live resource goes through OsSIdObjDel instead. Note this is NOT the
 * same contract the previous stub had (it forwarded to OsSIdObjDel). */
UINT32 LOS_SIdDelete(UINT32 sId)
{
    UINT32 intSave, ret;
    SIdObj *sIdObj = NULL;

    ret = OsSIdOwnerCheck(sId);
    if (ret != LOS_OK) {
        return ret;
    }

    SCHEDULER_LOCK(intSave);
    ret = OsSIdObjGet(sId, &sIdObj);
    if (ret != LOS_OK) {
        goto OUT_UNLOCK;
    }

    if ((OS_SL_LIST_FIRST(&sIdObj->ipcList) != NULL) ||
        (sIdObj->entity != NULL)) {
        ret = LOS_ERRNO_SEC_INVALID_SID;
        goto OUT_UNLOCK;
    }

    OsSIdReset(sIdObj);
    ret = LOS_OK;
OUT_UNLOCK:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

#endif /* LOSCFG_BOX */

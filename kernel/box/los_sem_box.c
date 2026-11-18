/*
 * los_sem_box.c — BOX (user-mode isolation) semaphore infrastructure.
 *
 * Same shape as the mux face (los_mux_box.c), applied to the semaphore:
 *  - per-box sem CB free lists over the box resource arrays
 *    (g_boxFreeSemList[boxId], defined in los_sem.c next to the kernel
 *    unused list — same placement as the mux free lists)
 *  - OsBoxSemInit / OsBoxSemListInit: OsBoxResSemInit callees
 *    (los_box.c; SID module registration + free-list build)
 *  - OsSemGetCB: sId -> CB resolution for the los_sem.c entry seams
 *    (kernel-head and box sIds, owner-checked)
 *  - OsSemCreateInternal: the ONE create core for EVERY context (refs shape).
 *    The pool is picked inside it from OsGetCurBoxCB(), so a kernel caller
 *    draws the kernel table and a box caller its own pool - the CB's pool and
 *    the SID slot's owner therefore cannot disagree. semId is stamped with the
 *    sId (so OsSemStateVerify still works under RES_CHECK)
 *
 * The pend/post core stays in los_sem.c (OsSchedWait / list wakeups). The
 * sem syscalls take free IPC-segment slots 80..83 (see syscalls_num.h).
 */
#include "los_sem_pri.h"
#include "los_sem_debug_pri.h"
#include "los_box_pri.h"
#include "los_task_pri.h"
#include "los_interrupt.h"
#include "los_sched_pri.h"
#include "los_sec_pri.h"
#include "securec.h"

#if (LOSCFG_BASE_IPC_SEM == 1) && defined(LOSCFG_BOX)

/* Add the box CB array to the box free list (same shape as the mux init). */
STATIC VOID OsSemInitInternal(LOS_DL_LIST *freeList, LosSemCB *semList, UINT32 listLen)
{
    UINT32 index;
    LosSemCB *semNode = NULL;

    for (index = 0; index < listLen; index++) {
        semNode = semList + index;
        semNode->semStat = LOS_UNUSED;
        semNode->semCount = 0;
        semNode->semType = OS_SEM_COUNTING;
        LOS_ListInit(&semNode->semList);
        /* refs OsSemInitInternal shape: semId carries the POOL INDEX until a
         * create overwrites it with the bound sId. OsSemCreateInternal reads
         * it as the dataCBIdx for OsSIdBind, which is why it must be the index
         * and not 0. Matches what los_sem.c's OsSemInit does for the kernel
         * table. */
        semNode->semId = index;
        LOS_ListTailInsert(freeList, &semNode->semList);
    }
}

/* OsBoxSemInit (same shape as OsBoxMuxInit): register the
 * box entity array with the SID module, then build the box free list over
 * the CB array. */
LITE_OS_SEC_TEXT_INIT UINT32 OsBoxSemInit(EntityObj *entityList, LosSemCB *semList,
                                          UINT32 listLen, UINT16 boxId)
{
    if ((entityList == NULL) || (semList == NULL) || (listLen == 0) ||
        (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return LOS_ERRNO_SEM_INVALID;
    }

    if (OsSIdModuleInit(LOS_SID_TYPE_SEM, 0, entityList, listLen, boxId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;
    }

    OsSemInitInternal(&g_boxFreeSemList[boxId], semList, listLen);
    return LOS_OK;
}

/* OsBoxResSemInit callee (los_box.c): init the box free list head. */
LITE_OS_SEC_TEXT UINT32 OsBoxSemListInit(UINT16 boxId)
{
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_NOK;
    }
    LOS_ListInit(&g_boxFreeSemList[boxId]);
    return LOS_OK;
}

/* OsGetMuxFreeList shape: interrupt / kernel context uses the kernel
 * list, a box context its own. */
LITE_OS_SEC_TEXT VOID OsGetSemFreeList(const LosBoxCB *boxCB, LOS_DL_LIST **freeList)
{
    *freeList = ((OS_INT_ACTIVE || (boxCB == NULL)) ? &g_unusedSemList :
                 &g_boxFreeSemList[boxCB->boxId]);
}

/* sId -> CB through the SID module (slot -> entity -> moduleData). Lock-free,
 * and the caller holds the scheduler lock - the refs name for this shape.
 * (refs also has OsSemGetCBLocked, a SELF-LOCKING wrapper; we have no caller
 * needing it, so it does not exist here.) Unlike the refs one-liner this also
 * rejects an unused CB: the SID layer only guarantees the slot is bound, not
 * that the CB behind it has not been recycled to LOS_UNUSED. */
LITE_OS_SEC_TEXT UINT32 OsSemGetCB(UINT32 sId, LosSemCB **semCreated)
{
    UINT32 ret;
    LosSemCB *semCB = NULL;

    ret = OsSIdGetDataCB(sId, LOS_SID_TYPE_SEM, (VOID **)&semCB);
    if (ret != LOS_OK) {
        return ret;
    }
    if (semCB->semStat == LOS_UNUSED) {
        return LOS_ERRNO_SEM_INVALID;
    }
    *semCreated = semCB;
    return LOS_OK;
}

/* The ONE sem create core, for every context (refs OsSemCreateInternal shape).
 *
 * Why the pool selection lives HERE and not at the public entries: it used to
 * be split - los_sem.c's OsSemCreate hardcoded g_unusedSemList for the
 * "kernel arm", this file had a near-copy OsSemCreateBox for the box arm, and
 * LOS_SemCreate / LOS_BinarySemCreate each repeated the routing condition
 * (!OS_INT_ACTIVE && OsGetCurBoxCB() != NULL). Every entry had to get that
 * condition right, and a miss would draw a kernel-table CB in box context -
 * which, now that the SID middle layer is live, makes OsSIdBind index the BOX
 * entity array with a KERNEL index (boxA SEM_LIMIT=2 vs kernel SEM_LIMIT=8)
 * and reject. Selecting the pool once, inside the core, from the same
 * OsGetCurBoxCB() that OsSIdCreate uses for the slot owner, makes the two
 * unable to disagree. This is also exactly how queue (OsQueueCreateOut) and
 * event (LOS_EventCreate) already work. */
LITE_OS_SEC_TEXT_INIT UINT32 OsSemCreateInternal(UINT16 count, UINT32 *semHandle, UINT8 isBinary)
{
    UINT32 intSave;
    UINT32 ret;
    UINT32 newSId;
    LOS_DL_LIST *freeList = NULL;
    LosSemCB *semCreated = NULL;

    if (semHandle == NULL) {
        return LOS_ERRNO_SEM_PTR_NULL;
    }
    /* the sId is written back to the caller: validate the pointer against
     * the current context first, hence the OsCheckValidDataAddr call here */
    if (OsCheckValidDataAddr((UINTPTR)semHandle, sizeof(UINT32)) != LOS_OK) {
        return LOS_ERRNO_SEM_PTR_NULL;
    }
    if (count > ((isBinary == OS_SEM_BINARY) ? OS_SEM_BINARY_COUNT_MAX : LOS_SEM_COUNT_MAX)) {
        return LOS_ERRNO_SEM_OVERFLOW;
    }

    SCHEDULER_LOCK(intSave);

    OsGetSemFreeList(OsGetCurBoxCB(), &freeList);
    if (LOS_ListEmpty(freeList)) {
        SCHEDULER_UNLOCK(intSave);
        OsSemInfoGetFullDataHook();
        return LOS_ERRNO_SEM_ALL_BUSY;
    }

    {
        LOS_DL_LIST *unusedSem = LOS_DL_LIST_FIRST(freeList);
        LOS_ListDelete(unusedSem);
        semCreated = GET_SEM_LIST(unusedSem);
    }
    semCreated->semStat = LOS_USED;
    semCreated->semType = isBinary;
    semCreated->semCount = count;
    LOS_ListInit(&semCreated->semList);

    /* semId still carries the POOL INDEX stamped at init (refs shape), which
     * is exactly the dataCBIdx OsSIdBind needs - the entity array is parallel
     * to the CB array in both bounds (see OsBoxSemInit / OsSemInit). It is
     * overwritten with the sId immediately after, and LOS_SemDelete restores
     * it on the way back to the free list. */
    ret = OsSIdGenObj(LOS_SID_TYPE_SEM, semCreated->semId, (VOID *)semCreated, &newSId);
    if (ret != LOS_OK) {
        semCreated->semStat = LOS_UNUSED;
        LOS_ListTailInsert(freeList, &semCreated->semList);
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_SEM_ALL_BUSY;
    }
    semCreated->semId = newSId;
    SCHEDULER_UNLOCK(intSave);

    OsSemDbgUpdateHook(semCreated->semId, OsCurrTaskGet()->taskEntry, count);
    *semHandle = newSId;
    return LOS_OK;
}

#endif /* (LOSCFG_BASE_IPC_SEM == 1) && LOSCFG_BOX */

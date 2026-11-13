/*
 * los_sem_box.c — BOX (user-mode isolation) semaphore infrastructure.
 *
 * M5G, the mux-face shape (kernel/base/los_mux_box.c) applied to the sem:
 *  - per-box sem CB free lists over the box resource arrays
 *    (g_boxFreeSemList[boxId], defined in los_sem.c next to the kernel
 *    unused list — same placement as the mux free lists)
 *  - OsBoxSemInit / OsBoxSemListInit: OsBoxResSemInit callees
 *    (los_box.c; SID module registration + free-list build)
 *  - OsSemGetCBLocked: sId -> CB resolution for the los_sem.c entry seams
 *    (kernel-head and box sIds, owner-checked)
 *  - OsSemCreateBox: the box-context create core (refs safe_ipc/los_sem.c
 *    OsSemCreateInternal shape) — CB from the owning box pool, flat SID
 *    from OsSIdGenObj (M5F stub, two-bound), semId stamped with the sId
 *    (OsSemStateVerify compatibility under RES_CHECK)
 *
 * The pend/post core stays in los_sem.c (OHOS OsSchedWait / list wakeups —
 * semantically the refs core). refs syscalls_def.h carries NO sem defs
 * (its IPC sem service is kernel/driver-box only), so the M5G block takes
 * free refs IPC-segment slots 80..83 (see syscalls_num.h).
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

/* add the box CB array to the box free list (OsMuxInitInternal shape;
 * refs OsSemInitInternal analog). */
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
        semNode->semId = 0;   /* replaced by the bound sId at create time */
        LOS_ListTailInsert(freeList, &semNode->semList);
    }
}

/* refs safe_ipc/los_sem.c OsBoxSemInit (OsBoxMuxInit shape): register the
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

/* refs los_box.c OsBoxResSemInit callee: init the box free list head. */
LITE_OS_SEC_TEXT UINT32 OsBoxSemListInit(UINT16 boxId)
{
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_NOK;
    }
    LOS_ListInit(&g_boxFreeSemList[boxId]);
    return LOS_OK;
}

/* refs OsGetSemFreeList (safe_ipc/los_sem.c; OsGetMuxFreeList shape):
 * interrupt / kernel context uses the kernel list, a box context its own. */
LITE_OS_SEC_TEXT VOID OsGetSemFreeList(const LosBoxCB *boxCB, LOS_DL_LIST **freeList)
{
    *freeList = ((OS_INT_ACTIVE || (boxCB == NULL)) ? &g_unusedSemList :
                 &g_boxFreeSemList[boxCB->boxId]);
}

/* OsSemGetCBLocked (OsMuxGetCBLocked shape): sId -> CB through the SID
 * stub (M5F two-bound flat). Callers hold the scheduler lock. */
LITE_OS_SEC_TEXT UINT32 OsSemGetCBLocked(UINT32 sId, LosSemCB **semCreated)
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

/* The box-context create core (refs safe_ipc/los_sem.c OsSemCreateInternal
 * box arm; OsMuxCreate shape). The caller (los_sem.c LOS_SemCreate seam)
 * has already range-checked count and validated the handle pointer. */
LITE_OS_SEC_TEXT_INIT UINT32 OsSemCreateBox(UINT16 count, UINT8 type, UINT32 *semHandle)
{
    UINT32 intSave;
    UINT32 ret;
    UINT32 newSId;
    LOS_DL_LIST *freeList = NULL;
    LosSemCB *semCreated = NULL;
    const LosBoxCB *boxCB = OsGetCurBoxCB();

    if (semHandle == NULL) {
        return LOS_ERRNO_SEM_PTR_NULL;
    }
    /* the sId is written back to the caller: validate the pointer against
     * the current context first (refs OsCheckValidDataAddr position) */
    if (OsCheckValidDataAddr((UINTPTR)semHandle, sizeof(UINT32)) != LOS_OK) {
        return LOS_ERRNO_SEM_PTR_NULL;
    }
    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_SEM_PEND_INTERR;
    }

    SCHEDULER_LOCK(intSave);
    OsGetSemFreeList(boxCB, &freeList);
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
    semCreated->semType = type;
    semCreated->semCount = count;
    LOS_ListInit(&semCreated->semList);

    /* entity array is parallel to the CB array (refs OsBoxSemInit) */
    ret = OsSIdGenObj(LOS_SID_TYPE_SEM, (UINT32)(semCreated - (LosSemCB *)boxCB->boxResSemList),
                      (VOID *)semCreated, &newSId);
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

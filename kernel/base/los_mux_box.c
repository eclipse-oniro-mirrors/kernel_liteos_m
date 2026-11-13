/*
 * los_mux_box.c — BOX (user-mode isolation) mux mechanisms.
 *
 * M5C, mirroring refs kernel_s/base/los_mux.c BOX parts + the refs
 * safe_ipc/los_queue.c pattern (the only full IPC implementation in the
 * snapshot):
 *  - per-box mux CB free lists over the box resource arrays
 *    (g_boxFreeMuxList[boxId], refs g_boxFreeQueueList analog)
 *  - OsBoxMuxInit / OsBoxMuxListInit: refs los_box.c OsBoxResMuxInit
 *    callees (SID module registration + free-list build)
 *  - OsMuxGetCBLocked: sId -> CB resolution for the los_mux.c seams
 *    (kernel-head and box sIds, owner-checked)
 *  - OsMuxCreate(sId, attr): the refs syscall entry — box tasks create
 *    muxes in their own pool; kernel tasks delegate to LOS_MuxCreate
 *    (kernel pool, kernel-head sId)
 *
 * The pend/post core stays in los_mux.c (OHOS OsMuxPendOp/OsMuxPostOp —
 * semantically the refs core).
 */
#include "los_mux_pri.h"
#include "los_mux_debug_pri.h"
#include "los_box_pri.h"
#include "los_task_pri.h"
#include "los_interrupt.h"
#include "los_sched_pri.h"
#include "securec.h"

#if (LOSCFG_BASE_IPC_MUX == 1) && defined(LOSCFG_BOX)

/* add muxList CBs to a free list (refs OsQueueInitInternal analog;
 * the body is missing from the snapshot — the obvious init loop). */
STATIC VOID OsMuxInitInternal(LOS_DL_LIST *freeList, LosMuxCB *muxList, UINT32 listLen)
{
    UINT32 index;
    LosMuxCB *muxNode = NULL;

    for (index = 0; index < listLen; index++) {
        muxNode = muxList + index;
        muxNode->muxCount = 0;
        muxNode->muxStat = LOS_UNUSED;
        muxNode->owner = NULL;
        muxNode->priority = 0;
        LOS_ListInit(&muxNode->muxList);
        muxNode->muxId = 0;   /* replaced by the bound sId at create time */
        LOS_ListTailInsert(freeList, &muxNode->muxList);
    }
}

/* refs los_mux.c contracts (OsBoxQueueInit shape in safe_ipc/los_queue.c):
 * register the box entity array with the SID module, then build the box
 * free list over the CB array. */
LITE_OS_SEC_TEXT_INIT UINT32 OsBoxMuxInit(EntityObj *entityList, LosMuxCB *muxList,
                                          UINT32 listLen, UINT16 boxId)
{
    if ((entityList == NULL) || (muxList == NULL) || (listLen == 0) ||
        (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return LOS_ERRNO_MUX_INVALID;
    }

    if (OsSIdModuleInit(LOS_SID_TYPE_MUX, 0, entityList, listLen, boxId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;
    }

    OsMuxInitInternal(&g_boxFreeMuxList[boxId], muxList, listLen);
    return LOS_OK;
}

/* refs los_box.c OsBoxResMuxInit callee: init the box free list head. */
LITE_OS_SEC_TEXT UINT32 OsBoxMuxListInit(UINT16 boxId)
{
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_NOK;
    }
    LOS_ListInit(&g_boxFreeMuxList[boxId]);
    return LOS_OK;
}

/* refs OsGetMuxFreeList (OsGetQueueFreeList shape in refs los_queue.c):
 * interrupt / kernel context uses the kernel list, a box context its own. */
LITE_OS_SEC_TEXT VOID OsGetMuxFreeList(const LosBoxCB *boxCB, LOS_DL_LIST **freeList)
{
    *freeList = ((OS_INT_ACTIVE || (boxCB == NULL)) ? &g_unusedMuxList :
                 &g_boxFreeMuxList[boxCB->boxId]);
}

/* refs OsMuxGetCBLocked: sId -> CB. Owner/type-checked through the SID
 * module (kernel-head sIds resolve for everyone; box sIds only for the
 * owning box). Callers hold the scheduler lock. */
LITE_OS_SEC_TEXT UINT32 OsMuxGetCBLocked(UINT32 sId, LosMuxCB **muxCreated)
{
    UINT32 ret;
    LosMuxCB *muxCB = NULL;

    ret = OsSIdGetDataCB(sId, LOS_SID_TYPE_MUX, (VOID **)&muxCB);
    if (ret != LOS_OK) {
        return ret;
    }
    if (muxCB->muxStat == LOS_UNUSED) {
        return LOS_ERRNO_MUX_INVALID;
    }
    *muxCreated = muxCB;
    return LOS_OK;
}

/* refs syscall entry (refs syscalls_def.h IPC_BASE+10): create a mux and
 * return its sId. Box tasks draw the CB from their own pool and the sId
 * from their own SID pool; kernel callers get the kernel pool via
 * LOS_MuxCreate. The OHOS mux core is recursive + priority-inherit, which
 * matches the attr the refs usr wrapper passes. */
LITE_OS_SEC_TEXT UINT32 OsMuxCreate(UINT32 *sId, UINT32 attr)
{
    UINT32 intSave;
    UINT32 ret;
    UINT32 newSId;
    LOS_DL_LIST *freeList = NULL;
    LosMuxCB *muxCreated = NULL;
    const LosBoxCB *boxCB = OsGetCurBoxCB();

    (VOID)attr;   /* OHOS core defaults == refs usr-wrapper attr */

    if (sId == NULL) {
        return LOS_ERRNO_MUX_PTR_NULL;
    }
    /* the sId is written back to the caller: validate the pointer against
     * the current context first (refs OsCheckValidDataAddr position) */
    if (OsCheckValidDataAddr((UINTPTR)sId, sizeof(UINT32)) != LOS_OK) {
        return LOS_ERRNO_MUX_PTR_NULL;
    }
    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_MUX_INVALID;
    }

    if (boxCB == NULL) {
        /* kernel context: kernel pool, kernel-head sId */
        return LOS_MuxCreate(sId);
    }

    SCHEDULER_LOCK(intSave);
    OsGetMuxFreeList(boxCB, &freeList);
    if (LOS_ListEmpty(freeList)) {
        SCHEDULER_UNLOCK(intSave);
        OsMutexCheckHook();
        return LOS_ERRNO_MUX_ALL_BUSY;
    }

    {
        LOS_DL_LIST *unusedMux = LOS_DL_LIST_FIRST(freeList);
        LOS_ListDelete(unusedMux);
        muxCreated = LOS_DL_LIST_ENTRY(unusedMux, LosMuxCB, muxList);
    }
    muxCreated->muxCount = 0;
    muxCreated->muxStat = LOS_USED;
    muxCreated->owner = NULL;
    muxCreated->priority = 0;
    LOS_ListInit(&muxCreated->muxList);

    /* entity array is parallel to the CB array (refs OsBoxMuxInit) */
    ret = OsSIdGenObj(LOS_SID_TYPE_MUX, (UINT32)(muxCreated - (LosMuxCB *)boxCB->boxResMuxList),
                      (VOID *)muxCreated, &newSId);
    if (ret != LOS_OK) {
        muxCreated->muxStat = LOS_UNUSED;
        LOS_ListTailInsert(freeList, &muxCreated->muxList);
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_MUX_ALL_BUSY;
    }
    muxCreated->muxId = newSId;
    SCHEDULER_UNLOCK(intSave);

    OsMuxDbgUpdateHook(muxCreated->muxId, OsCurrTaskGet()->taskEntry);
    *sId = newSId;
    return LOS_OK;
}

/* refs syscalls_def.h IPC_BASE+14. Reconstruction (refs body missing):
 * reports whether the mux is currently held by any task. */
LITE_OS_SEC_TEXT UINT32 LOS_MuxHeld(UINT32 sId)
{
    UINT32 intSave;
    UINT32 ret;
    UINT32 held;
    LosMuxCB *muxCB = NULL;

    SCHEDULER_LOCK(intSave);
    ret = OsMuxGetCBLocked(sId, &muxCB);
    held = ((ret == LOS_OK) && (muxCB->owner != NULL)) ? 1U : 0U;
    SCHEDULER_UNLOCK(intSave);
    return held;
}

#endif /* (LOSCFG_BASE_IPC_MUX == 1) && LOSCFG_BOX */

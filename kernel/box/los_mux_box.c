/*
 * los_mux_box.c — BOX (user-mode isolation) mux mechanisms.
 *
 * Contents:
 *  - per-box mux CB free lists over the box resource arrays
 *    (g_boxFreeMuxList[boxId])
 *  - OsBoxMuxInit / OsBoxMuxListInit: the OsBoxResMuxInit callees in
 *    los_box.c (SID module registration + free-list build)
 *  - OsMuxGetCB: sId -> CB resolution for the los_mux.c entry seams
 *    (kernel-head and box sIds, owner-checked)
 *  - OsMuxCreate(sId, attr): the ONE mux create core for EVERY context (refs
 *    shape). The pool is picked inside it from OsGetCurBoxCB(), so a kernel
 *    caller draws the kernel table and a box caller its own pool - the CB's
 *    pool and the SID slot's owner therefore cannot disagree. LOS_MuxCreate
 *    (los_mux.c) and the usr-world wrapper (los_usr_api.c) both funnel here.
 *
 * The pend/post core stays in los_mux.c (OsMuxPendOp / OsMuxPostOp).
 */
#include "los_mux_pri.h"
#include "los_mux_debug_pri.h"
#include "los_box_pri.h"
#include "los_task_pri.h"
#include "los_interrupt.h"
#include "los_sched_pri.h"
#include "securec.h"

#if (LOSCFG_BASE_IPC_MUX == 1) && defined(LOSCFG_BOX)

/* add muxList CBs to a free list: the obvious init loop. */
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
        /* refs OsMuxInitInternal shape: muxId carries the POOL INDEX until a
         * create overwrites it with the bound sId. OsMuxCreate reads it as the
         * dataCBIdx for OsSIdBind, which is why it must be the index and not 0.
         * Matches what los_mux.c's OsMuxInit does for the kernel table. */
        muxNode->muxId = index;
        LOS_ListTailInsert(freeList, &muxNode->muxList);
    }
}

/* Register the box entity array with the SID module, then build the box
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

/* OsBoxResMuxInit callee (los_box.c): init the box free list head. */
LITE_OS_SEC_TEXT UINT32 OsBoxMuxListInit(UINT16 boxId)
{
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_NOK;
    }
    LOS_ListInit(&g_boxFreeMuxList[boxId]);
    return LOS_OK;
}

/* Interrupt / kernel context uses the kernel list, a box context its own. */
LITE_OS_SEC_TEXT VOID OsGetMuxFreeList(const LosBoxCB *boxCB, LOS_DL_LIST **freeList)
{
    *freeList = ((OS_INT_ACTIVE || (boxCB == NULL)) ? &g_unusedMuxList :
                 &g_boxFreeMuxList[boxCB->boxId]);
}

/* sId -> CB. Owner/type-checked through the SID module (kernel-head sIds
 * resolve for everyone; box sIds only for the owning box). Lock-free, and the
 * caller holds the scheduler lock - the refs name for this shape. (refs also
 * has OsMuxGetCBLocked, a SELF-LOCKING wrapper; we have no caller needing it,
 * so it does not exist here.) Unlike the refs one-liner this also rejects an
 * unused CB: the SID layer only guarantees the slot is bound, not that the CB
 * behind it has not been recycled to LOS_UNUSED. */
LITE_OS_SEC_TEXT UINT32 OsMuxGetCB(UINT32 sId, LosMuxCB **muxCreated)
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

/* The ONE mux create core, for every context (refs OsMuxCreate shape).
 *
 * Why the pool selection lives HERE: it used to be split - this function
 * handled the box arm and delegated a kernel context to LOS_MuxCreate, which
 * hardcoded g_unusedMuxList. That meant two near-identical create bodies, and
 * more importantly the CB's pool was chosen by a DIFFERENT rule than the SID
 * slot's owner (OsSIdCreate uses OsGetCurBoxCB()). Picking the pool here, from
 * the same OsGetCurBoxCB(), makes the two unable to disagree - which matters
 * now that OsSIdBind indexes the owner's entity array with the CB's pool
 * index (boxB MUX_LIMIT=1 vs kernel MUX_LIMIT=8 would overflow on a mismatch).
 * This is also how queue (OsQueueCreateOut) and event (LOS_EventCreate) and
 * now sem (OsSemCreateInternal) all work.
 *
 * attr is ignored: the kernel mux core is recursive + priority-inherit by
 * construction, which is what the user-world wrapper passes. */
LITE_OS_SEC_TEXT UINT32 OsMuxCreate(UINT32 *sId, UINT32 attr)
{
    UINT32 intSave;
    UINT32 ret;
    UINT32 newSId;
    LOS_DL_LIST *freeList = NULL;
    LosMuxCB *muxCreated = NULL;

    (VOID)attr;   /* core defaults == the user-world wrapper's attr */

    if (sId == NULL) {
        return LOS_ERRNO_MUX_PTR_NULL;
    }
    /* the sId is written back to the caller: validate the pointer against
     * the current context first */
    if (OsCheckValidDataAddr((UINTPTR)sId, sizeof(UINT32)) != LOS_OK) {
        return LOS_ERRNO_MUX_PTR_NULL;
    }
    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_MUX_INVALID;
    }

    SCHEDULER_LOCK(intSave);
    OsGetMuxFreeList(OsGetCurBoxCB(), &freeList);
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

    /* muxId still carries the POOL INDEX stamped at init (refs
     * OsMuxInitInternal shape) - that is the dataCBIdx OsSIdBind needs, the
     * entity array being parallel to the CB array in both bounds (see
     * OsBoxMuxInit / OsMuxInit). Overwritten with the sId right after, and
     * restored by LOS_MuxDelete on the way back to the free list. */
    ret = OsSIdGenObj(LOS_SID_TYPE_MUX, muxCreated->muxId, (VOID *)muxCreated, &newSId);
    if (ret != LOS_OK) {
        muxCreated->muxStat = LOS_UNUSED;
        LOS_ListTailInsert(freeList, &muxCreated->muxList);
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_MUX_ALL_BUSY;
    }
    muxCreated->muxId = newSId;
    SCHEDULER_UNLOCK(intSave);

    OsMuxDbgUpdateHook(muxCreated->muxId, OsCurrTaskGet()->taskEntry);
    /* No LOS_TRACE / OsHookCall here: refs' OsMuxCreate carries only the trace
     * (which is empty in this build - LOSCFG_KERNEL_TRACE is off) and never the
     * create hook, and los_trace.h is not on this module's include path. The
     * pre-merge kernel arm in los_mux.c had both, but no create hook is
     * registered and LOSCFG_BASE_IPC_MUX_CREATOR is off, so nothing observable
     * changes. */
    *sId = newSId;
    return LOS_OK;
}

/* refs OsMuxGetCBLocked, verbatim: the SELF-LOCKING wrapper around OsMuxGetCB.
 * It was deliberately absent until the condvar needed it - OsCondVarWait must
 * resolve the mutex OUTSIDE its own scheduler-lock hold (it then drops and
 * re-takes the mux around the wait), so the caller-locked OsMuxGetCB does not
 * fit there. Both spellings now exist, exactly as in refs. */
LITE_OS_SEC_TEXT UINT32 OsMuxGetCBLocked(UINT32 sId, LosMuxCB **muxCreated)
{
    UINT32 intSave;
    UINT32 ret;

    SCHEDULER_LOCK(intSave);
    ret = OsMuxGetCB(sId, muxCreated);
    SCHEDULER_UNLOCK(intSave);

    return ret;
}

/* Reports whether the mux is currently held by any task. */
LITE_OS_SEC_TEXT UINT32 LOS_MuxHeld(UINT32 sId)
{
    UINT32 intSave;
    UINT32 ret;
    UINT32 held;
    LosMuxCB *muxCB = NULL;

    SCHEDULER_LOCK(intSave);
    ret = OsMuxGetCB(sId, &muxCB);
    held = ((ret == LOS_OK) && (muxCB->owner != NULL)) ? 1U : 0U;
    SCHEDULER_UNLOCK(intSave);
    return held;
}

#endif /* (LOSCFG_BASE_IPC_MUX == 1) && LOSCFG_BOX */

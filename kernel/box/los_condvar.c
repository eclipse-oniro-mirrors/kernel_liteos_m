/*
 * los_condvar.c — BOX condition variable, ported from refs
 * kernel_s/base/safe_ipc/los_condvar.c.
 *
 * Shape: identical to the other four SID modules - a kernel table plus one
 * pool per box, an EntityObj array parallel to each CB array, one SID slot per
 * live object, and the pool chosen ONCE inside the create entry from
 * OsGetCurBoxCB() (the M6L rule: the CB's pool and the SID slot's owner must
 * come from the same predicate in the same place, or OsSIdBind indexes the
 * wrong entity array).
 *
 * What is adapted rather than copied verbatim is listed at each site and
 * summarised in the design doc (M6M). The three that matter:
 *
 *  - the waiter queue reuses the event bodies los_event_sid.c already
 *    maintains (OsEventReadSid / OsEventWriteLockedSid) instead of refs'
 *    STATIC OsEventRead / OsEventWrite in the baseline los_event.c, so no
 *    baseline file has to be touched;
 *  - the mux release/re-take around the wait goes through the sId-form public
 *    entries (LOS_MuxPost / LOS_MuxPend), because this tree has no self-locking
 *    CB-form mux pend/post; that keeps every para-check and hook refs' OsMuxPend
 *    would have run;
 *  - refs' two PRINT_WARN calls in OsCondVarWait are dropped. This entry is a
 *    syscall, so it runs on the 1024B box kernel stack inside the svc handler,
 *    where the printf chain overflows it (design doc pit 54-5). The warnings
 *    reported a mux post/pend that cannot fail for a mutex the caller already
 *    holds, so nothing observable is lost. The crash signature to recognise if
 *    this is ever reintroduced: PC inside the string pool, then a second
 *    exception nested in the first.
 */
#include "los_typedef.h"
#include "los_condvar_pri.h"
#include "los_interrupt.h"      /* OS_INT_ACTIVE */
#include "los_atomic.h"         /* LOS_AtomicCmpXchg32bits */
#include "los_event_sid.h"      /* OsEventReadSid / OsEventWriteLockedSid */
#include "los_mux_pri.h"        /* OsMuxGetCBLocked / OsMuxErrorCheckFlag */
#include "los_mux.h"            /* LOS_MuxPend / LOS_MuxPost (sId form) */
#include "los_task_pri.h"       /* OsCurrTaskGet / SCHEDULER_LOCK */
#include "los_task.h"           /* LOS_Schedule */
#include "los_box_pri.h"        /* OsGetCurBoxCB / OsCheckValidDataAddr */
#include "los_memory.h"         /* LOS_MemAlloc / m_aucSysMem0 */
#include "los_sec_pri.h"        /* SID contracts */
#include "securec.h"

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_IPC_CONDVAR)

#define COND_EVENT_VAL        0x1U
#define COND_COUNTER_STEP     0x0004
#define COND_FLAGS_MASK       0x0003U
#define COND_COUNTER_MASK     (~COND_FLAGS_MASK)
#define COND_FLAGS_INITED     0x1
#define COND_FLAGS_DESTROYED  0x0
/* refs puts this #define down beside OsCondWaitSub; hoisted here with the
 * rest so the value layout is in one place. */
#define COND_EVENT_MASK       0x0fU

/* Kernel free list plus one per box. The free link travels in the embedded
 * EVENT_CB_S's own stEventList, exactly as the event module does. */
STATIC LOS_DL_LIST g_freeCondList;
STATIC LOS_DL_LIST g_freeBoxCondList[LOSCFG_BASE_BOX_MAX_LIMIT];

/* Kernel-side table (the other bound), allocated from the system heap at init
 * - same placement pattern as g_osAllSem / g_osAllEvent. The kernel entity
 * array stays STATIC here: nothing outside this TU needs it, and exporting it
 * would widen the kernel RAM surface for no reason. */
LITE_OS_SEC_BSS LosCondVarCB *g_osAllCond = NULL;
LITE_OS_SEC_BSS STATIC EntityObj g_kernelCondEntityArr[LOSCFG_BASE_IPC_CONDVAR_LIMIT];

STATIC_INLINE VOID OsEventNodeRecycle(PEVENT_CB_S eventNode, LOS_DL_LIST *freeList)
{
    LOS_ListTailInsert(freeList, &eventNode->stEventList);
}

/* refs OsCondInitInternal. One addition: the embedded event list head is
 * initialised before the node is linked, matching what OsBoxEventPoolInit does
 * for the event pools (refs relies on LOS_ListTailInsert not reading the node's
 * old pointers, which is true but worth not depending on). */
LITE_OS_SEC_TEXT STATIC VOID OsCondInitInternal(LOS_DL_LIST *freeList, LosCondVarCB *condList, UINT32 listLen)
{
    LosCondVarCB *condCB = NULL;
    UINT32 idx;

    for (idx = 0; idx < listLen; idx++) {
        condCB = condList + idx;
        condCB->eventNode.uwEventID = 0;
        LOS_ListInit(&condCB->eventNode.stEventList);
        condCB->condCBId = idx;
        condCB->count = 0;
        condCB->value = COND_FLAGS_DESTROYED;
        OsEventNodeRecycle(&(condCB->eventNode), freeList);
    }
}

/* refs OsCondEventCreate: take a node off the pool's free list. Returns the
 * embedded EVENT_CB_S, which is also the LosCondVarCB address because eventNode
 * is the first member. */
LITE_OS_SEC_TEXT STATIC PEVENT_CB_S OsCondEventCreate(UINT32 *ret, LOS_DL_LIST *freeList)
{
    LOS_DL_LIST *unusedEvt = NULL;
    PEVENT_CB_S eventCreated = NULL;
    *ret = LOS_OK;

    if (LOS_ListEmpty(freeList)) {
        *ret = LOS_ERRNO_COND_MAX_NUM_LIMIT;
        goto OUT;
    }

    unusedEvt = LOS_DL_LIST_FIRST(freeList);
    eventCreated = GET_COND_EVENT_LIST(unusedEvt);
    eventCreated->uwEventID = 0;
    LOS_ListDelete(unusedEvt);
    LOS_ListInit(&eventCreated->stEventList);

OUT:
    return eventCreated;
}

/* OsIpcInit callee, mirroring OsEventInit / OsSemInit: allocate the kernel
 * table, register the kernel entity array under LOS_KERNEL_SID_HEAD, build the
 * kernel free list. refs instead LOS_ASSERTs that a statically provided table
 * and entity array are non-NULL (its KERNEL_COND_LIMIT / g_kernelCondEntity come
 * from a config header outside the snapshot); allocating here matches what this
 * tree already does for sem/queue/event. */
LITE_OS_SEC_TEXT_INIT UINT32 OsCondInit(VOID)
{
    if (LOSCFG_BASE_IPC_CONDVAR_LIMIT == 0) {
        return LOS_ERRNO_COND_MAX_NUM_LIMIT;
    }

    g_osAllCond = (LosCondVarCB *)LOS_MemAlloc(m_aucSysMem0,
                        LOSCFG_BASE_IPC_CONDVAR_LIMIT * sizeof(LosCondVarCB));
    if (g_osAllCond == NULL) {
        return LOS_ERRNO_COND_MAX_NUM_LIMIT;
    }
    (VOID)memset_s(g_osAllCond, LOSCFG_BASE_IPC_CONDVAR_LIMIT * sizeof(LosCondVarCB),
                   0, LOSCFG_BASE_IPC_CONDVAR_LIMIT * sizeof(LosCondVarCB));

    /* perm mode 0: LOSCFG_BASE_IPC_PERM is off, so OsSIdModuleInit ignores the
     * argument - the same value the other four modules pass here. */
    if (OsSIdModuleInit(LOS_SID_TYPE_COND, 0, g_kernelCondEntityArr,
                        (UINT32)LOSCFG_BASE_IPC_CONDVAR_LIMIT,
                        (UINT16)LOS_KERNEL_SID_HEAD) != LOS_OK) {
        return LOS_ERRNO_COND_INIT_SID;
    }

    LOS_ListInit(&g_freeCondList);
    OsCondInitInternal(&g_freeCondList, g_osAllCond, (UINT32)LOSCFG_BASE_IPC_CONDVAR_LIMIT);

    return LOS_OK;
}

/* refs OsCondResDestroy. Only ever reached through a destroyCb registered with
 * OsSIdRegModuleHandle, which is the empty inline in this build
 * (LOSCFG_EXC_ISOLATION off) - kept for shape parity with refs. */
LITE_OS_SEC_TEXT STATIC UINT32 OsCondResDestroy(const VOID *boxCB, const VOID *entityCB, VOID *recoverCB)
{
    (VOID)recoverCB;
    (VOID)boxCB;
    (VOID)entityCB;

    return OS_SID_RES_DESTROYED;
}

/* OsBoxResCondInit callee, mirroring OsBoxSemInit: register the box entity
 * array with the SID module, then build the box free list over the CB array. */
LITE_OS_SEC_TEXT_INIT UINT32 OsBoxCondInit(EntityObj *entityList, LosCondVarCB *condList,
                                           UINT32 listLen, UINT16 boxId)
{
    SIdModuleHandle handle;

    if ((entityList == NULL) || (condList == NULL) || (listLen == 0) ||
        (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return LOS_ERRNO_COND_PTR_NULL;
    }

    if (OsSIdModuleInit(LOS_SID_TYPE_COND, 0, entityList, listLen, boxId) != LOS_OK) {
        return LOS_ERRNO_COND_INIT_SID;
    }

    (VOID)memset(&handle, 0, sizeof(SIdModuleHandle));
    handle.destroyCb = OsCondResDestroy;
    OsSIdRegModuleHandle(LOS_SID_TYPE_COND, boxId, &handle);

    OsCondInitInternal(&g_freeBoxCondList[boxId], condList, listLen);

    return LOS_OK;
}

/* OsBoxResCondInit callee (los_box.c): init the box free list head. */
LITE_OS_SEC_TEXT UINT32 OsBoxCondListInit(UINT16 boxId)
{
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_NOK;
    }
    LOS_ListInit(&g_freeBoxCondList[boxId]);
    return LOS_OK;
}

/* OsGetSemFreeList shape: interrupt / kernel context uses the kernel list, a
 * box context its own pool. STATIC - only this TU calls it, so the LosBoxCB
 * parameter never leaks into a header box user code includes. */
STATIC LITE_OS_SEC_TEXT VOID OsGetCondVarFreeList(const LosBoxCB *boxCB, LOS_DL_LIST **freeList)
{
    *freeList = ((OS_INT_ACTIVE || (boxCB == NULL)) ? &g_freeCondList :
                 &g_freeBoxCondList[boxCB->boxId]);
}

/* refs OsCondVarCreate. *sId is an in/out parameter: pass 0 to have a fresh
 * sId allocated, or a sId previously obtained from LOS_GenSId to bind to that
 * pre-allocated slot (the refs pre-generated arm, which is what gives
 * OsSIdKernelResIntCheck and OsSIdObjGet a caller in this tree). */
UINT32 OsCondVarCreate(UINT32 *sId)
{
    UINT32 intSave, ret, newSId;
    SIdObj *sIdObj = NULL;
    PEVENT_CB_S eventCreated = NULL;
    LosCondVarCB *condCB = NULL;
    LOS_DL_LIST *freeList = NULL;

    if (sId == NULL) {
        return LOS_ERRNO_COND_PTR_NULL;
    }

    if (OsCheckValidDataAddr((UINTPTR)sId, sizeof(UINT32)) != LOS_OK) {
        return LOS_ERRNO_COND_INVAL_ADDR;
    }

    SCHEDULER_LOCK(intSave);
    newSId = *sId;
    if (OsSIdOwnerAndTypeCheck(newSId, (UINT32)LOS_SID_TYPE_COND) == LOS_OK) {
        /* only a kernel resource may be created from an interrupt */
        ret = OsSIdKernelResIntCheck(newSId);
        if (ret != LOS_OK) {
            goto FAILED;
        }
        if (OsSIdObjGet(newSId, &sIdObj) != LOS_OK) {
            newSId = 0;
        }
    } else {
        newSId = 0;
    }

    OsGetCondVarFreeList(OsGetCurBoxCB(), &freeList);
    eventCreated = OsCondEventCreate(&ret, freeList);
    if (eventCreated == NULL) {
        goto FAILED;
    }
    condCB = (LosCondVarCB *)(UINTPTR)eventCreated;

    /* condCBId still carries the pool index stamped at init - that is the
     * dataCBIdx OsSIdBind wants, the entity array being parallel to the CB
     * array in both bounds. */
    if (newSId == 0) {
        /* refs reports LOS_ERRNO_EVENT_GEN_SID_OBJ here; this tree has no such
         * code (the event module never grew one), so the condvar's own
         * init-SID errno stands in. */
        if (OsSIdGenObj((UINT32)LOS_SID_TYPE_COND, condCB->condCBId, (VOID *)condCB, &newSId) != LOS_OK) {
            OsEventNodeRecycle(eventCreated, freeList);
            ret = LOS_ERRNO_COND_INIT_SID;
            goto FAILED;
        }
    } else {
        if (OsSIdBind((UINT32)LOS_SID_TYPE_COND, sIdObj, condCB->condCBId, (VOID *)condCB) != LOS_OK) {
            OsEventNodeRecycle(eventCreated, freeList);
            ret = LOS_ERRNO_COND_INIT_SID;
            goto FAILED;
        }
    }

    *sId = newSId;

    condCB->value = COND_FLAGS_INITED;
    condCB->count = 0;

    /* refs finishes with OS_CHECK_REMAIN_RESOURCE(OsGetCurBoxCB(),
     * RESOURCE_TYPE_COND); this tree has no per-box resource accounting (none
     * of the other four modules call it either), so the line is dropped. */
    ret = LOS_OK;
FAILED:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

/* refs OsCondVarDelete. Note the ordering: the owner is read from the ENTITY
 * before OsSIdObjDel, because ObjDel clears the entity - same rule the sem and
 * mux delete paths follow. */
UINT32 OsCondVarDelete(UINT32 sId)
{
    UINT32 ret;
    UINT32 intSave;
    UINT32 ownerBoxId;
    PEVENT_CB_S eventCB = NULL;
    LosCondVarCB *cond = NULL;
    LOS_DL_LIST *freeList = NULL;

    ret = OsSIdOwnerAndTypeCheck(sId, (UINT32)LOS_SID_TYPE_COND);
    if (ret != LOS_OK) {
        return ret;
    }

    SCHEDULER_LOCK(intSave);
    ret = OsEntityOwnerCheckbySId(sId);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    ret = OsSIdGetDataCB(sId, (UINT32)LOS_SID_TYPE_COND, (VOID **)(UINTPTR)&cond);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }
    eventCB = &cond->eventNode;
    if (!LOS_ListEmpty(&eventCB->stEventList) || (cond->count > 0)) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_COND_SHOULD_NOT_DESTORY;
    }

    ownerBoxId = OsEntityOwnerGetBySid(sId);
    if (ownerBoxId == (UINT32)LOS_KERNEL_SID_HEAD) {
        freeList = &g_freeCondList;
    } else if (ownerBoxId < (UINT32)LOSCFG_BASE_BOX_MAX_LIMIT) {
        freeList = &g_freeBoxCondList[ownerBoxId];
    } else {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_COND_DEL_SID_OBJ;
    }

    ret = OsSIdObjDel(sId);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_COND_DEL_SID_OBJ;
    }

    eventCB->uwEventID = 0;
    LOS_ListTailInsert(freeList, &eventCB->stEventList);

    cond->value = COND_FLAGS_DESTROYED;
    cond->count = 0;

    SCHEDULER_UNLOCK(intSave);

    return ret;
}

/* refs OsCondValueModify: bump the change counter above the flag bits without
 * disturbing them, via a CAS loop. A waiter that sampled `value` before
 * releasing the mutex compares afterwards - a difference means a signal landed
 * in the window, so it does not block. */
STATIC VOID OsCondValueModify(LosCondVarCB *cond)
{
    UINT32 flags = ((UINT32)cond->value & COND_FLAGS_MASK);
    INT32 oldVal, newVal;

    for (;;) {
        oldVal = cond->value;
        newVal = (INT32)(((UINT32)(oldVal - COND_COUNTER_STEP) & COND_COUNTER_MASK) | flags);
        /* LOS_AtomicCmpXchg32bits returns FALSE when the swap happened (the
         * original equalled oldVal), which is refs' "== 0" success test. */
        if (LOS_AtomicCmpXchg32bits((Atomic *)&cond->value, newVal, oldVal) == FALSE) {
            break;
        }
    }
}

/* refs OsCondVarSignal. The value bump is done with the lock DROPPED (it is a
 * CAS loop) and the lock is re-taken for the event write, exactly as refs does
 * it. */
UINT32 OsCondVarSignal(UINT32 sId, BOOL isBroadcast)
{
    UINT32 ret;
    BOOL once;
    BOOL needSched = FALSE;
    UINT32 intSave;
    LosCondVarCB *cond = NULL;
    UINT8 exitFlag = FALSE;

    SCHEDULER_LOCK(intSave);
    ret = OsSIdGetDataCB(sId, (UINT32)LOS_SID_TYPE_COND, (VOID **)(UINTPTR)&cond);
    if ((ret != LOS_OK) || (cond->count <= 0)) {
        goto EXIT;
    }

    if (isBroadcast == TRUE) {
        cond->count = 0;
        once = FALSE;
    } else {
        cond->count--;
        once = TRUE;
    }
    SCHEDULER_UNLOCK(intSave);

    OsCondValueModify(cond);

    SCHEDULER_LOCK(intSave);
    /* refs calls OsEventWrite(cb, val, once) and uses its return as "someone
     * was woken"; this tree's event body reports that through an out-parameter
     * instead (los_event_sid.c's locked-write shape). */
    ret = OsEventWriteLockedSid(&cond->eventNode, COND_EVENT_VAL, once, &needSched);
    exitFlag = needSched;

EXIT:
    SCHEDULER_UNLOCK(intSave);

    if (exitFlag == TRUE) {
        LOS_Schedule();
    }

    return ret;
}

/* refs OsCondWaitSub: re-check the sampled value, and only block if nothing
 * changed. A signal that arrived while the mutex was released shows up as
 * value != oldValue, so the waiter clears the event bit and returns success
 * without ever going to sleep. */
STATIC UINT32 OsCondWaitSub(LosCondVarCB *cond, INT32 oldValue, UINT32 ticks)
{
    UINT32 ret = LOS_OK;
    UINT32 intSave;
    PEVENT_CB_S eventCB = &cond->eventNode;

    SCHEDULER_LOCK(intSave);
    if (cond->value != oldValue) {
        eventCB->uwEventID &= ~COND_EVENT_VAL;
        goto OUT;
    }

    ret = OsEventReadSid(eventCB, COND_EVENT_MASK, LOS_WAITMODE_OR | LOS_WAITMODE_CLR, ticks);

OUT:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

/* refs OsCountSub: undo the waiter registration on a failed/timed-out wait. */
STATIC VOID OsCountSub(LosCondVarCB *cond)
{
    UINT32 intSave;
    SCHEDULER_LOCK(intSave);
    if (cond->count > 0) {
        cond->count--;
    }
    SCHEDULER_UNLOCK(intSave);
}

/* refs OsCondVarWait. mutexId is the sId of a mutex the CALLER already holds;
 * it is released around the wait and re-taken before returning, which is the
 * whole point of a condvar. absTicks is passed straight through as the event
 * read timeout (refs names it absTicks but OsEventRead takes a relative tick
 * count - kept as refs spells it so the two stay comparable). */
UINT32 OsCondVarWait(UINT32 sId, UINT32 mutexId, UINT32 absTicks)
{
    INT32 oldValue;
    UINT32 intSave;
    UINT32 ret;
    LosMuxCB *muxPended = NULL;
    const LosTaskCB *runTask = NULL;
    LosCondVarCB *cond = NULL;

    /* A condvar wait blocks, so an ISR can never be a legal caller. refs relies
     * on the mux entry to reject it; stating it here keeps the errno refs
     * defined for exactly this case in use. */
    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_COND_READ_IN_INTERRUPT;
    }

    ret = OsMuxGetCBLocked(mutexId, &muxPended);
    if (ret != LOS_OK) {
        return ret;
    }

    runTask = (const LosTaskCB *)OsCurrTaskGet();
    /* OsMuxErrorCheckFlag is the FALSE fallback in this build (no muxAttr), so
     * this test short-circuits off - identical to refs under the same config.
     * Kept verbatim so turning on LOSCFG_COMPAT_POSIX would enable it. */
    if (OsMuxErrorCheckFlag(muxPended) && (runTask != muxPended->owner)) {
        return LOS_ERRNO_COND_NOT_OWNER_PEND;
    }

    SCHEDULER_LOCK(intSave);
    ret = OsSIdGetDataCB(sId, (UINT32)LOS_SID_TYPE_COND, (VOID **)(UINTPTR)&cond);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    oldValue = cond->value;
    cond->count++;
    SCHEDULER_UNLOCK(intSave);

    /* Release and re-take the mutex through the sId-form public entries: this
     * tree has no self-locking CB-form OsMuxPend/OsMuxPost (refs does), and the
     * sId entries carry every para-check, INT guard and dbg hook that refs'
     * CB-form would have run. The cost is one extra SID resolve each way.
     * refs logs a PRINT_WARN on failure; dropped here because this runs in the
     * svc handler on the 1024B box kernel stack, where the printf chain
     * overflows it (pit 54-5). */
    (VOID)LOS_MuxPost(mutexId);

    ret = OsCondWaitSub(cond, oldValue, absTicks);

    (VOID)LOS_MuxPend(mutexId, LOS_WAIT_FOREVER);

    switch (ret) {
        case 0:
        case COND_EVENT_VAL:
            ret = LOS_OK;
            break;
        default:
            OsCountSub(cond);
            break;
    }

    return ret;
}

#endif /* LOSCFG_BOX && LOSCFG_BASE_IPC_CONDVAR */

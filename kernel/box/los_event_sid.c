/*
 * los_event_sid.c — SID-addressed events: per-box event pools plus the
 * task-embedded event read/write paths (OsEventReadWithId /
 * OsEventWriteWithId / OsEventGetCB over CURTASK, TASK and EVENT
 * addressing) and the LOS_TaskEventRead / LOS_TaskEventWrite wrappers.
 *
 * Baseline-copy discipline: the Poll/ParamCheck/Read/Write helpers below
 * mirror the baseline bodies in los_event.c - when the baseline changes,
 * sync the two. Differences from the baseline copies:
 *  1. no internal SCHEDULER_LOCK/UNLOCK / LOS_Schedule (the WithId callers
 *     hold the scheduler lock; WriteLocked outputs needSched instead);
 *  2. TRACE/hook calls run under the lock on the SID paths (locating the
 *     CB must stay inside the lock, moving hooks out would open a TOCTOU
 *     gap between locating and operating on the CB).
 *
 * Guarded: BOX=off builds compile an empty translation unit.
 */
#include "los_event_sid.h"
#include "los_typedef.h"
#include "los_error.h"
#include "los_event.h"
#include "los_task_pri.h"
#include "los_box_pri.h"    /* encoded box id resolution (box TCB pools) */
#include "los_sec_pri.h"    /* OsSIdGenObj/OsSIdGetDataCB/OsSIdObjDel */
#include "los_memory.h"     /* LOS_MemAlloc for the kernel event table */
#include "los_interrupt.h"  /* OS_INT_ACTIVE (free-list context split) */
#include "securec.h"       /* memset_s (pool init) */

/* defined below (after the pool block): the SID pool family wraps them */
extern UINT32 OsEventReadWithId(UINT32 sId, UINT32 eventType, UINT32 eventMask,
                                UINT32 mode, UINT32 timeout);
extern UINT32 OsEventWriteWithId(UINT32 sId, UINT32 events, BOOL once,
                                 UINT32 eventType, UINT32 retMode);

#if defined(LOSCFG_BOX) && (LOSCFG_BASE_IPC_EVENT == 1)

/* ---------------- baseline copies (los_event.c) ---------------- */

STATIC UINT32 OsEventParamCheckSid(const VOID *ptr, UINT32 eventMask, UINT32 mode)
{
    if (ptr == NULL) {
        return LOS_ERRNO_EVENT_PTR_NULL;
    }
    if (eventMask == 0) {
        return LOS_ERRNO_EVENT_EVENTMASK_INVALID;
    }
    if (eventMask & LOS_ERRTYPE_ERROR) {
        return LOS_ERRNO_EVENT_SETBIT_INVALID;
    }
    if (((mode & LOS_WAITMODE_OR) && (mode & LOS_WAITMODE_AND)) ||
        (mode & ~(LOS_WAITMODE_OR | LOS_WAITMODE_AND | LOS_WAITMODE_CLR)) ||
        !(mode & (LOS_WAITMODE_OR | LOS_WAITMODE_AND))) {
        return LOS_ERRNO_EVENT_FLAGS_INVALID;
    }
    return LOS_OK;
}

STATIC UINT32 OsEventPollSid(UINT32 *eventId, UINT32 eventMask, UINT32 mode)
{
    UINT32 ret = 0;

    LOS_ASSERT(ArchIntLocked());
    LOS_ASSERT(LOS_SpinHeld(&g_taskSpin));

    if (mode & LOS_WAITMODE_OR) {
        if ((*eventId & eventMask) != 0) {
            ret = *eventId & eventMask;
        }
    } else {
        if ((eventMask != 0) && (eventMask == (*eventId & eventMask))) {
            ret = *eventId & eventMask;
        }
    }

    if (ret && (mode & LOS_WAITMODE_CLR)) {
        *eventId = *eventId & ~ret;
    }

    return ret;
}

/* baseline OsEventRead body, lock held by the caller. Non-STATIC: the condvar
 * (los_condvar.c) waits on its embedded EVENT_CB_S through this, which is why
 * it is exported rather than each caller keeping its own copy. */
UINT32 OsEventReadSid(PEVENT_CB_S eventCB, UINT32 eventMask, UINT32 mode, UINT32 timeout)
{
    UINT32 ret;
    LosTaskCB *runTask = OsCurrTaskGet();

    ret = OsEventPollSid(&eventCB->uwEventID, eventMask, mode);
    if (ret == 0) {
        if (timeout == 0) {
            return ret;
        }
        if (!OsPreemptableInSched()) {
            return LOS_ERRNO_EVENT_READ_IN_LOCK;
        }
        runTask->eventMask = eventMask;
        runTask->eventMode = mode;
        OsSchedWait(runTask, &eventCB->stEventList, timeout);
        if (runTask->taskStatus & OS_TASK_STATUS_TIMEOUT) {
            runTask->taskStatus &= ~OS_TASK_STATUS_TIMEOUT;
            return LOS_ERRNO_EVENT_READ_TIMEOUT;
        }
        ret = OsEventPollSid(&eventCB->uwEventID, eventMask, mode);
    }
    return ret;
}

/* baseline OsEventWrite body minus lock/schedule. Non-STATIC: the condvar
 * signals its embedded EVENT_CB_S through this (refs calls its own
 * OsEventWrite the same way), and *needSched tells the caller whether to
 * LOS_Schedule() once the lock is dropped. */
UINT32 OsEventWriteLockedSid(PEVENT_CB_S eventCB, UINT32 events, BOOL once, BOOL *needSched)
{
    LosTaskCB *resumedTask = NULL;
    LosTaskCB *nextTask = NULL;
    UINT8 exitFlag = 0;

    if (eventCB == NULL) {
        return LOS_ERRNO_EVENT_PTR_NULL;
    }
    if ((eventCB->stEventList.pstNext == NULL) || (eventCB->stEventList.pstPrev == NULL)) {
        return LOS_ERRNO_EVENT_NOT_INITIALIZED;
    }
    if (events & LOS_ERRTYPE_ERROR) {
        return LOS_ERRNO_EVENT_SETBIT_INVALID;
    }

    eventCB->uwEventID |= events;
    if (!LOS_ListEmpty(&eventCB->stEventList)) {
        for (resumedTask = LOS_DL_LIST_ENTRY((&eventCB->stEventList)->pstNext, LosTaskCB, pendList);
             &resumedTask->pendList != &eventCB->stEventList;) {
            nextTask = LOS_DL_LIST_ENTRY(resumedTask->pendList.pstNext, LosTaskCB, pendList);
            if (((resumedTask->eventMode & LOS_WAITMODE_OR) && ((resumedTask->eventMask & events) != 0)) ||
                ((resumedTask->eventMode & LOS_WAITMODE_AND) &&
                 ((resumedTask->eventMask & eventCB->uwEventID) == resumedTask->eventMask))) {
                exitFlag = 1;
                OsSchedWake(resumedTask);
            }
            if (once == TRUE) {
                break;
            }
            resumedTask = nextTask;
        }
    }
    *needSched = (exitFlag == 1);
    return LOS_OK;
}

/* ---------------- id-addressed paths ---------------- */

/* Task-id validity check covering the encoded box id space: box tasks
 * carry OS_BOX_SET_ID-encoded ids (the box swtmr task is one - its wake-up
 * write targets it), so ids past the kernel array resolve through the box
 * TCB pools instead of being rejected. */
STATIC UINT32 OsTaskIdSecCheck(UINT32 taskId)
{
    UINT32 taskStatus;
    UINT32 taskIdx;
    LosBoxCB *boxCB = NULL;
    LosTaskCB *taskCB = NULL;

    if (taskId < (UINT32)LOSCFG_BASE_CORE_TSK_LIMIT) {
        taskStatus = OS_TCB_FROM_TID(taskId)->taskStatus;
    } else {
        taskIdx = OS_BOX_GET_ID(taskId);
        if (OS_BOX_GET_BID(taskId) >= LOSCFG_BASE_BOX_MAX_LIMIT) {
            return LOS_ERRNO_EVENT_SEC_TSK_INVALID;
        }
        boxCB = &g_userBoxCBArr[OS_BOX_GET_BID(taskId)];
        if ((taskIdx >= boxCB->boxResTaskNum) || (boxCB->boxResTaskList == NULL)) {
            return LOS_ERRNO_EVENT_SEC_TSK_INVALID;
        }
        taskCB = &((LosTaskCB *)(VOID *)boxCB->boxResTaskList)[taskIdx];
        /* the box pool stores the ENCODED id in the TCB (OS_BOX_SET_ID at
         * pool init) - compare encoded against encoded */
        if (taskCB->taskId != taskId) {
            return LOS_ERRNO_EVENT_SEC_TSK_INVALID;
        }
        taskStatus = taskCB->taskStatus;
    }
    if ((taskStatus & OS_TASK_STATUS_UNUSED) != 0) {
        return LOS_ERRNO_EVENT_SEC_TSK_INVALID;
    }
    return LOS_OK;
}

/* Per-box event pool free lists. Nodes are the box's LosEventCB
 * array; the free link travels in eventNode.stEventList. */
STATIC LOS_DL_LIST g_evtFreeList[LOSCFG_BASE_BOX_MAX_LIMIT];

/* Kernel-side table + free list (the other bound of the two-bound shape).
 * Same placement pattern as g_osAllSem/g_unusedSemList: the table is
 * allocated from the system heap at init, the free link travels in the node's
 * own stEventList. */
LITE_OS_SEC_BSS LosEventCB *g_osAllEvent = NULL;
STATIC LITE_OS_SEC_BSS LOS_DL_LIST g_evtKernelFreeList;
/* Kernel event entity array, registered with the SID module under
 * LOS_KERNEL_SID_HEAD (same shape as g_kernelMuxEntityArr in los_mux.c):
 * one EntityObj per kernel-table event CB, parallel to g_osAllEvent. */
LITE_OS_SEC_BSS STATIC EntityObj g_kernelEvtEntityArr[LOSCFG_BASE_IPC_EVENT_LIMIT];

/* OsIpcInit callee, mirroring OsSemInit: allocate the table, stamp each
 * node's pool index, init the embedded EVENT_CB_S, link it into the kernel
 * free list. */
LITE_OS_SEC_TEXT_INIT UINT32 OsEventInit(VOID)
{
    LosEventCB *eventNode = NULL;
    UINT32 index;

    if (LOSCFG_BASE_IPC_EVENT_LIMIT == 0) {
        return LOS_ERRNO_EVENT_SEC_MAX_NUM_LIMIT;
    }

    g_osAllEvent = (LosEventCB *)LOS_MemAlloc(m_aucSysMem0,
                                              LOSCFG_BASE_IPC_EVENT_LIMIT * sizeof(LosEventCB));
    if (g_osAllEvent == NULL) {
        return LOS_ERRNO_EVENT_SEC_MAX_NUM_LIMIT;
    }

    LOS_ListInit(&g_evtKernelFreeList);

    /* Register the kernel event entity array with the SID module under the
     * kernel SID head, so a kernel-table event CB binds against the kernel
     * entity array (OsSIdModuleInit shape from OsMuxInit/OsSemInit). */
    if (OsSIdModuleInit(LOS_SID_TYPE_EVT, 0, g_kernelEvtEntityArr,
                        (UINT32)LOSCFG_BASE_IPC_EVENT_LIMIT,
                        (UINT16)LOS_KERNEL_SID_HEAD) != LOS_OK) {
        return LOS_ERRNO_EVENT_SEC_MAX_NUM_LIMIT;
    }

    for (index = 0; index < (UINT32)LOSCFG_BASE_IPC_EVENT_LIMIT; index++) {
        eventNode = g_osAllEvent + index;
        eventNode->eventCBId = index;
        (VOID)LOS_EventInit(&eventNode->eventNode);
        LOS_ListTailInsert(&g_evtKernelFreeList, &eventNode->eventNode.stEventList);
    }
    return LOS_OK;
}

/* OsGetSemFreeList shape: interrupt / kernel context uses the kernel list, a
 * box context its own pool. STATIC - only this TU's create path calls it, so
 * the LosBoxCB parameter never leaks into a public header. */
STATIC LITE_OS_SEC_TEXT VOID OsGetEventFreeList(const LosBoxCB *boxCB, LOS_DL_LIST **freeList)
{
    *freeList = ((OS_INT_ACTIVE || (boxCB == NULL)) ? &g_evtKernelFreeList :
                 &g_evtFreeList[boxCB->boxId]);
}

/* Box-core hook (OsBoxResEventInit), mirroring OsBoxSemInit: register the
 * box entity array with the SID module, then build the box free list over
 * the CB array. */
LITE_OS_SEC_TEXT_INIT UINT32 OsBoxEventInit(EntityObj *entityList, LosEventCB *evtList,
                                            UINT32 listLen, UINT16 boxId)
{
    UINT32 i;

    if ((entityList == NULL) || (evtList == NULL) || (listLen == 0) ||
        (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return LOS_ERRNO_EVENT_SEC_MAX_NUM_LIMIT;
    }

    if (OsSIdModuleInit(LOS_SID_TYPE_EVT, 0, entityList, listLen, boxId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;
    }

    LOS_ListInit(&g_evtFreeList[boxId]);
    for (i = 0; i < listLen; i++) {
        evtList[i].eventCBId = i;
        (VOID)LOS_EventInit(&evtList[i].eventNode);
        LOS_ListTailInsert(&g_evtFreeList[boxId], &evtList[i].eventNode.stEventList);
    }
    return LOS_OK;
}

/* OsBoxResEventInit callee (los_box.c): init the box free list head. */
LITE_OS_SEC_TEXT UINT32 OsBoxEventListInit(UINT16 boxId)
{
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_NOK;
    }
    LOS_ListInit(&g_evtFreeList[boxId]);
    return LOS_OK;
}

/* Locate the event CB for the addressing type; the EVENT arm resolves the
 * sId through the SID core (slot -> entity -> the pool node). */
STATIC UINT32 OsEventGetCB(UINT32 sId, PEVENT_CB_S *eventCB, UINT32 eventType)
{
    UINT32 ret;

    if (eventType == (UINT32)OS_EVENT_ID_TYPE_CURTASK) {
        *eventCB = &OsCurrTaskGet()->event;
        return LOS_OK;
    }

    if (eventType == (UINT32)OS_EVENT_ID_TYPE_TASK) {
        ret = OsTaskIdSecCheck(sId);
        if (ret != LOS_OK) {
            return ret;
        }
        if (sId < (UINT32)LOSCFG_BASE_CORE_TSK_LIMIT) {
            *eventCB = &OS_TCB_FROM_TID(sId)->event;
        } else {
            /* encoded box id: resolve the pool TCB (validated above) */
            LosBoxCB *boxCB = &g_userBoxCBArr[OS_BOX_GET_BID(sId)];
            LosTaskCB *taskCB = &((LosTaskCB *)(VOID *)boxCB->boxResTaskList)[OS_BOX_GET_ID(sId)];
            *eventCB = &taskCB->event;
        }
        return LOS_OK;
    }

    if (eventType != (UINT32)OS_EVENT_ID_TYPE_EVENT) {
        return LOS_ERRNO_EVENT_SEC_ID_TYPE_INVALID;
    }
    return OsSIdGetDataCB(sId, (UINT32)LOS_SID_TYPE_EVT, (VOID **)eventCB);
}

/* ---- SID event-pool family (flat-sId handles) ---- */

LITE_OS_SEC_TEXT UINT32 LOS_EventCreate(UINT32 *sId)
{
    UINT32 intSave;
    UINT32 newSId;
    UINT32 secRet;
    const LosBoxCB *boxCB = NULL;
    LOS_DL_LIST *freeList = NULL;
    LosEventCB *eventNode = NULL;
    PEVENT_CB_S eventCreated = NULL;

    if (sId == NULL) {
        return LOS_ERRNO_EVENT_PTR_NULL;
    }

    SCHEDULER_LOCK(intSave);

    /* Kernel/interrupt context draws from the kernel table, a box context
     * from its own pool - so a kernel-created event gets a kernel-head sId
     * that any box can resolve (refs LOS_EventCreate shape). */
    boxCB = OsGetCurBoxCB();
    OsGetEventFreeList(boxCB, &freeList);
    if ((freeList->pstNext == NULL) || LOS_ListEmpty(freeList)) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_EVENT_SEC_MAX_NUM_LIMIT;
    }

    /* free link travels in eventNode.stEventList; EVENT_CB_S is the first
     * member of LosEventCB, so container == node */
    eventCreated = LOS_DL_LIST_ENTRY(LOS_DL_LIST_FIRST(freeList),
                                     EVENT_CB_S, stEventList);
    eventNode = (LosEventCB *)(VOID *)eventCreated;
    LOS_ListDelete(LOS_DL_LIST_FIRST(freeList));
    eventCreated->uwEventID = 0;
    LOS_ListInit(&eventCreated->stEventList);

    secRet = OsSIdGenObj((UINT32)LOS_SID_TYPE_EVT, eventNode->eventCBId,
                         (VOID *)eventCreated, &newSId);
    if (secRet != LOS_OK) {
        LOS_ListTailInsert(freeList, &eventCreated->stEventList);
        SCHEDULER_UNLOCK(intSave);
        return secRet;
    }

    SCHEDULER_UNLOCK(intSave);

    *sId = newSId;
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 LOS_EventDelete(UINT32 sId)
{
    UINT32 ret;
    UINT32 intSave;
    PEVENT_CB_S eventCB = NULL;
    LOS_DL_LIST *freeList = NULL;
    UINT16 boxId;

    if (OS_GET_SID_TYPE(sId) != (UINT32)LOS_SID_TYPE_EVT) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }
    /* Recycle into the list the node came from: a kernel-head sId belongs to
     * the kernel table, a box sId to that box's pool. */
    boxId = OS_GET_SID_BOXID(sId);
    if (boxId == (UINT16)LOS_KERNEL_SID_HEAD) {
        freeList = &g_evtKernelFreeList;
    } else if (boxId < (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        freeList = &g_evtFreeList[boxId];
    } else {
        return LOS_ERRNO_EVENT_SEC_DEL_SID_OBJ;
    }
    if (freeList->pstNext == NULL) {
        return LOS_ERRNO_EVENT_SEC_DEL_SID_OBJ;
    }

    SCHEDULER_LOCK(intSave);

    ret = OsSIdGetDataCB(sId, (UINT32)LOS_SID_TYPE_EVT, (VOID **)&eventCB);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    if (!LOS_ListEmpty(&eventCB->stEventList)) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_EVENT_SHOULD_NOT_DESTROYED;
    }

    eventCB->uwEventID = 0;
    LOS_ListTailInsert(freeList, &eventCB->stEventList);

    /* Unbind the SID middle layer: clears the entity (owner / moduleData) and
     * frees the pool slot, so this sId stops resolving and the slot can be
     * handed out again. Same position sem/mux/queue take, inside the same
     * lock hold. */
    (VOID)OsSIdObjDel(sId);

    SCHEDULER_UNLOCK(intSave);
    return LOS_OK;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_EventPollWithSId(UINT32 sId, UINT32 eventMask, UINT32 mode)
{
    UINT32 ret;
    UINT32 intSave;
    PEVENT_CB_S eventCB = NULL;

    SCHEDULER_LOCK(intSave);

    ret = OsSIdGetDataCB(sId, (UINT32)LOS_SID_TYPE_EVT, (VOID **)&eventCB);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    ret = OsEventParamCheckSid((const VOID *)eventCB, eventMask, mode);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    ret = OsEventPollSid(&eventCB->uwEventID, eventMask, mode);
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_EventReadWithSId(UINT32 sId, UINT32 eventMask,
                                                   UINT32 mode, UINT32 timeout)
{
    return OsEventReadWithId(sId, (UINT32)OS_EVENT_ID_TYPE_EVENT, eventMask, mode, timeout);
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_EventWriteWithSId(UINT32 sId, UINT32 events)
{
    return OsEventWriteWithId(sId, events, FALSE, (UINT32)OS_EVENT_ID_TYPE_EVENT,
        (UINT32)OS_EVENT_MODE_RETURN_NONE);
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_EventWriteOnceWithSId(UINT32 sId, UINT32 events)
{
    return OsEventWriteWithId(sId, events, TRUE, (UINT32)OS_EVENT_ID_TYPE_EVENT,
        (UINT32)OS_EVENT_MODE_RETURN_NONE);
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_EventClearWithSId(UINT32 sId, UINT32 events)
{
    UINT32 ret;
    UINT32 intSave;
    PEVENT_CB_S eventCB = NULL;

    SCHEDULER_LOCK(intSave);

    ret = OsSIdGetDataCB(sId, (UINT32)LOS_SID_TYPE_EVT, (VOID **)&eventCB);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    eventCB->uwEventID &= events;
    SCHEDULER_UNLOCK(intSave);
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 OsEventReadWithId(UINT32 sId, UINT32 eventType, UINT32 eventMask,
                                          UINT32 mode, UINT32 timeout)
{
    UINT32 ret;
    UINT32 intSave;
    PEVENT_CB_S eventCB = NULL;
    LosTaskCB *runTask = NULL;

    /* Read path rejects TASK addressing: a task exit does not drain the
     * waiters parked on its embedded event, so reading by a dead task id
     * would create an orphan waiter that blocks forever. */
    if (eventType == (UINT32)OS_EVENT_ID_TYPE_TASK) {
        return LOS_ERRNO_EVENT_SEC_TSK_INVALID;
    }

    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_EVENT_READ_IN_INTERRUPT;
    }

    runTask = OsCurrTaskGet();
    if (runTask->taskFlags & OS_TASK_FLAG_SYSTEM) {
        PRINT_DEBUG("Warning: DO NOT recommend to use %s in system tasks.\n", __FUNCTION__);
        return LOS_ERRNO_EVENT_READ_IN_SYSTEM_TASK;
    }

    SCHEDULER_LOCK(intSave);

    ret = OsEventGetCB(sId, &eventCB, eventType);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    ret = OsEventParamCheckSid((const VOID *)eventCB, eventMask, mode);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    if ((eventCB->stEventList.pstNext == NULL) || (eventCB->stEventList.pstPrev == NULL)) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_EVENT_NOT_INITIALIZED;
    }

    ret = OsEventReadSid(eventCB, eventMask, mode, timeout);
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

LITE_OS_SEC_TEXT UINT32 OsEventWriteWithId(UINT32 sId, UINT32 events, BOOL once,
                                           UINT32 eventType, UINT32 retMode)
{
    UINT32 ret;
    UINT32 retVal = LOS_OK;
    UINT32 intSave;
    BOOL needSched = FALSE;
    PEVENT_CB_S eventCB = NULL;

    if (events & LOS_ERRTYPE_ERROR) {
        return LOS_ERRNO_EVENT_SETBIT_INVALID;
    }

    SCHEDULER_LOCK(intSave);

    ret = OsEventGetCB(sId, &eventCB, eventType);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    if (retMode == (UINT32)OS_EVENT_MODE_RETURN_BEFORE) {
        retVal = eventCB->uwEventID;
    }

    ret = OsEventWriteLockedSid(eventCB, events, once, &needSched);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }

    if (retMode == (UINT32)OS_EVENT_MODE_RETURN_AFTER) {
        retVal = eventCB->uwEventID;
    }

    SCHEDULER_UNLOCK(intSave);

    if (needSched == TRUE) {
        LOS_Schedule();
    }

    return retVal;
}

/* Wrappers: fixed addressing + return modes */
LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskEventRead(UINT32 event, UINT32 timeout)
{
    return OsEventReadWithId(0, (UINT32)OS_EVENT_ID_TYPE_CURTASK, event,
        (LOS_WAITMODE_OR | LOS_WAITMODE_CLR), timeout);
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_TaskEventWrite(UINT32 taskId, UINT32 event)
{
    return OsEventWriteWithId(taskId, event, FALSE, (UINT32)OS_EVENT_ID_TYPE_TASK,
        (UINT32)OS_EVENT_MODE_RETURN_NONE);
}

#endif /* LOSCFG_BOX && LOSCFG_BASE_IPC_EVENT */

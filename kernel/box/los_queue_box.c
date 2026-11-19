/*
 * los_queue_box.c - BOX (user-mode isolation) queue infrastructure.
 *
 * Same shape as the sem/mux faces (los_sem_box.c / los_mux_box.c):
 *  - per-box queue CB free lists over the box resource arrays
 *    (g_boxFreeQueueList[boxId], defined in los_queue.c next to the kernel
 *    free list - same placement as the mux/sem lists)
 *  - OsBoxQueueInit / OsBoxQueueListInit: OsBoxResQueInit callees
 *    (los_box.c; SID module registration + free-list build)
 *  - OsGetQueueFreeList: the box-context pool pick for the create arm and
 *    the failed-create reclaim (refs safe_ipc shape)
 *
 * The create/operate core stays in los_queue.c; entry resolution runs
 * through OsSIdGetDataCB (OsQueueResolve), so this file carries no resolve
 * helper of its own.
 */
#include "los_queue_pri.h"
#include "los_box_pri.h"
#include "los_interrupt.h"
#include "los_sec_pri.h"
#include "los_task_base.h"
#include "securec.h"

#if (LOSCFG_BASE_IPC_QUEUE == 1) && defined(LOSCFG_BOX)

/* Add the box CB array to the box free list (same shape as the sem/mux
 * init). queueID stays the IN-POOL index: the sId bound at create time
 * derives owner + index from the CB address and the field (OsSIdGenObj /
 * OsSIdGetDataCB), so both sides agree without re-stamping. */
STATIC VOID OsQueueInitInternal(LOS_DL_LIST *freeList, LosQueueCB *queList, UINT32 listLen)
{
    UINT32 index;

    for (index = 0; index < listLen; index++) {
        LosQueueCB *queNode = queList + index;
        queNode->queueID = (UINT16)index;
        queNode->queueState = OS_QUEUE_UNUSED;
        LOS_ListInit(&queNode->readWriteList[OS_QUEUE_READ]);
        LOS_ListInit(&queNode->readWriteList[OS_QUEUE_WRITE]);
        LOS_ListInit(&queNode->memList);
        LOS_ListTailInsert(freeList, &queNode->readWriteList[OS_QUEUE_WRITE]);
    }
}

/* OsBoxResQueInit callee (same shape as OsBoxSemInit): register the box
 * entity array with the SID module, then build the box free list over the
 * CB array. */
LITE_OS_SEC_TEXT_INIT UINT32 OsBoxQueueInit(EntityObj *entityList, LosQueueCB *queList,
                                            UINT32 listLen, UINT16 boxId)
{
    if ((entityList == NULL) || (queList == NULL) || (listLen == 0) ||
        (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return LOS_ERRNO_QUEUE_INVALID;
    }

    if (OsSIdModuleInit(LOS_SID_TYPE_QUE, 0, entityList, listLen, boxId) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SIDMODULE;
    }

    OsQueueInitInternal(&g_boxFreeQueueList[boxId], queList, listLen);
    return LOS_OK;
}

/* OsBoxResQueInit callee (los_box.c): init the box free list head. */
LITE_OS_SEC_TEXT UINT32 OsBoxQueueListInit(UINT16 boxId)
{
    if (boxId >= (UINT16)LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_NOK;
    }
    LOS_ListInit(&g_boxFreeQueueList[boxId]);
    return LOS_OK;
}

/* Box-context pool pick: interrupt / kernel context uses the kernel list,
 * a box context its own pool. */
LITE_OS_SEC_TEXT VOID OsGetQueueFreeList(UINTPTR boxCB, LOS_DL_LIST **freeList)
{
    *freeList = ((OS_INT_ACTIVE || (boxCB == (UINTPTR)NULL)) ?
                (&g_freeQueueList) : (&g_boxFreeQueueList[((LosBoxCB *)boxCB)->boxId]));
}

/* ---------------------------------------------------------------------------
 * refs port (M6T): LOS_QueueCreateWithSId / LOS_QueueInfoAllGet
 * (refs kernel_s/base/safe_ipc/los_queue.c:141 / :487). Ported verbatim in
 * shape; the deltas vs refs are all recorded here:
 *  - queueMem / queueInfo usr-address validation (OsCheckValidDataAddr)
 *    left out: kernel-only entry points in this tree for now. refs lists
 *    both APIs on its kernel syscall surface - promote the declarations to
 *    the public header together with the veneers when the usr world needs
 *    them (refs arch_s .../kernel/syscalls.h carries their numbers).
 *  - OS_CHECK_REMAIN_RESOURCE dropped (no resource accounting here, the
 *    M6M condvar precedent) and LOS_TRACE dropped (the M6L mux precedent).
 *  - Flat OHOS LosQueueCB fields instead of refs' info.* nesting; the CB
 *    pick/bind sequence reuses the M6L free-list shape.
 * ---------------------------------------------------------------------------
 */

/* Create a queue bound to a PRE-GENERATED sId - the second half of the
 * "interrupts may only touch pre-created sIds" path (LOS_GenSId creates
 * the slot, this binds a queue to it). Message memory is CALLER-PROVIDED;
 * the CB still comes from the pool the sId's boxId field names (kernel
 * head -> kernel list) - the OHOS g_staticQueue second pool is never
 * involved, this is the refs static-queue model. */
LITE_OS_SEC_TEXT UINT32 LOS_QueueCreateWithSId(UINT16 len, UINT32 sId, UINT32 flags,
                                               UINT16 maxMsgSize, UINT8 *queueMem, UINT16 memSize)
{
    UINT32 ret;
    UINT32 intSave;
    LosQueueCB *queueCB = NULL;
    SIdObj *sIdObj = NULL;
    LOS_DL_LIST *freeList = NULL;
    LOS_DL_LIST *unusedQueue = NULL;
    UINT16 msgSize;
    (VOID)flags;

    /* refs OsQueueCreateParameterCheck (refs base/los_queue.c:57), inlined. */
    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_QUEUE_CALLED_INTERR;
    }
    if (queueMem == NULL) {
        return LOS_ERRNO_QUEUE_CREAT_PTR_NULL;
    }
    if (maxMsgSize > (OS_NULL_SHORT - sizeof(UINT32))) {
        return LOS_ERRNO_QUEUE_SIZE_TOO_BIG;
    }
    if ((len == 0) || (maxMsgSize == 0)) {
        return LOS_ERRNO_QUEUE_PARA_ISZERO;
    }
    msgSize = maxMsgSize + (UINT16)sizeof(UINT32);
    if ((memSize / msgSize) < len) {
        return LOS_ERRNO_QUEUE_CREATE_NO_MEMORY;
    }

    if (OsSIdOwnerAndTypeCheck(sId, LOS_SID_TYPE_QUE) != LOS_OK) {
        return LOS_ERRNO_SEC_INVALID_SID;
    }

    ret = OsSIdKernelResIntCheck(sId);
    if (ret != LOS_OK) {
        return ret;
    }

    SCHEDULER_LOCK(intSave);
    if (OsSIdObjGet(sId, &sIdObj) != LOS_OK) {
        ret = LOS_ERRNO_SEC_INVALID_SID;
        goto OUT_UNLOCK;
    }

    OsGetQueueFreeList(OS_SID_BID_IS_KER(sId) ?
                       (UINTPTR)NULL : (UINTPTR)(&g_boxCBArr[OS_GET_SID_BOXID(sId)]),
                       &freeList);
    if (LOS_ListEmpty(freeList)) {
        ret = LOS_ERRNO_QUEUE_CB_UNAVAILABLE;
        goto OUT_UNLOCK;
    }
    unusedQueue = LOS_DL_LIST_FIRST(freeList);
    LOS_ListDelete(unusedQueue);
    queueCB = GET_QUEUE_LIST(unusedQueue);

    queueCB->queueLen = len;
    queueCB->queueSize = msgSize;
    queueCB->queue = queueMem;
    queueCB->queueState = OS_QUEUE_INUSED;
    queueCB->queueMemType = OS_QUEUE_ALLOC_STATIC;   /* refs: never heap-freed */
    queueCB->readWriteableCnt[OS_QUEUE_READ] = 0;
    queueCB->readWriteableCnt[OS_QUEUE_WRITE] = len;
    queueCB->queueHead = 0;
    queueCB->queueTail = 0;
    LOS_ListInit(&queueCB->readWriteList[OS_QUEUE_READ]);
    LOS_ListInit(&queueCB->readWriteList[OS_QUEUE_WRITE]);
    LOS_ListInit(&queueCB->memList);

    /* Bind the PRE-GENERATED sId; dataCBIdx is the pool index the CB came
     * from (queueCB->queueID still holds it - the create exit only rewrites
     * the OUTPUT handle, never the CB field). Failure recycles the CB back
     * to the list it came from (the OsQueueSecCreateOut shape). */
    ret = OsSIdBind((UINT32)LOS_SID_TYPE_QUE, sIdObj, queueCB->queueID, (VOID *)queueCB);
    if (ret != LOS_OK) {
        queueCB->queue = (UINT8 *)NULL;
        queueCB->queueState = OS_QUEUE_UNUSED;
        LOS_ListTailInsert(freeList, &queueCB->readWriteList[OS_QUEUE_WRITE]);
        ret = LOS_ERRNO_QUEUE_GEN_SID_OBJ;
    }

OUT_UNLOCK:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

/* refs base/los_queue.c:361 (OsQueueInfoGet): export the queue header plus
 * the wait-task bitmap. Deltas: flat OHOS CB fields; tskCB->taskId used
 * directly (OHOS task ids carry no box encoding, refs wraps them in
 * OS_BOX_GET_ID); SCHEDULER_LOCK for CRITICAL_ENTER. uwQueueID is the CB's
 * pool index - the sId lives in the entity table, not in the CB. */
STATIC UINT32 OsQueueInfoExport(UINT32 taskNum, const LosQueueCB *queueCB,
                                QUEUE_HEAD_INFO_S *queueInfo, UINT32 length)
{
    UINT32 intSave;
    UINT32 ret = LOS_OK;
    UINT32 taskId = 0;
    const LosTaskCB *tskCB = NULL;

    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_QUEUE_CALLED_INTERR;
    }

    SCHEDULER_LOCK(intSave);

    if ((queueCB->queueState & OS_QUEUE_UNUSED) != 0) {
        ret = LOS_ERRNO_QUEUE_NOT_CREATE;
        goto OUT_UNLOCK;
    }
    (VOID)memset_s((VOID *)queueInfo, length, 0, length);
    queueInfo->uwQueueID = queueCB->queueID;
    queueInfo->usQueueLen = queueCB->queueLen;
    queueInfo->usQueueSize = queueCB->queueSize;
    queueInfo->usQueueHead = queueCB->queueHead;
    queueInfo->usQueueTail = queueCB->queueTail;
    queueInfo->usReadableCnt = queueCB->readWriteableCnt[OS_QUEUE_READ];
    queueInfo->usWritableCnt = queueCB->readWriteableCnt[OS_QUEUE_WRITE];

    LOS_DL_LIST_FOR_EACH_ENTRY(tskCB, &queueCB->readWriteList[OS_QUEUE_READ], LosTaskCB, pendList) {
        taskId = tskCB->taskId;
        if (taskNum > taskId) {
            queueInfo->usWaitTask[GET_READ_ARRAY_IDX(taskId, taskNum)] |= (UINT32)GET_ARRAY_BIT(taskId);
        }
    }

    LOS_DL_LIST_FOR_EACH_ENTRY(tskCB, &queueCB->readWriteList[OS_QUEUE_WRITE], LosTaskCB, pendList) {
        taskId = tskCB->taskId;
        if (taskNum > taskId) {
            queueInfo->usWaitTask[GET_WRITE_ARRAY_IDX(taskId, taskNum)] |= (UINT32)GET_ARRAY_BIT(taskId);
        }
    }

    LOS_DL_LIST_FOR_EACH_ENTRY(tskCB, &queueCB->memList, LosTaskCB, pendList) {
        taskId = tskCB->taskId;
        if (taskNum > taskId) {
            queueInfo->usWaitTask[GET_MEM_ARRAY_IDX(taskId, taskNum)] |= (UINT32)GET_ARRAY_BIT(taskId);
        }
    }

OUT_UNLOCK:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

/* refs safe_ipc/los_queue.c:487: sId-resolved header + wait-task export.
 * The buffer must be QUEUE_ALL_INFO_SIZE(taskNum) bytes or larger. */
LITE_OS_SEC_TEXT_MINOR UINT32 LOS_QueueInfoAllGet(UINT32 sId, UINT32 taskNum,
                                                  QUEUE_HEAD_INFO_S *queueInfo, UINT32 length)
{
    UINT32 ret;
    UINT32 intSave;
    LosQueueCB *queueCB = NULL;

    if (queueInfo == NULL) {
        return LOS_ERRNO_QUEUE_INVALID_ADDR;
    }

    if (QUEUE_ALL_INFO_SIZE(taskNum) > length) {
        return LOS_ERRNO_QUEUE_INFO_SIZE_TOO_SMALL;
    }

    SCHEDULER_LOCK(intSave);
    ret = OsSIdGetDataCB(sId, (UINT32)LOS_SID_TYPE_QUE, (VOID **)&queueCB);
    if (ret != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return ret;
    }
    SCHEDULER_UNLOCK(intSave);

    return OsQueueInfoExport(taskNum, queueCB, queueInfo, length);
}

#endif /* (LOSCFG_BASE_IPC_QUEUE == 1) && defined(LOSCFG_BOX) */

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

#endif /* (LOSCFG_BASE_IPC_QUEUE == 1) && defined(LOSCFG_BOX) */

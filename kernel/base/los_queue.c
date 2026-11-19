/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2022 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "los_queue.h"
#include "los_queue_debug_pri.h"
#ifdef LOSCFG_BOX
#include "los_queue_pri.h"   /* OsGetQueueFreeList / g_boxFreeQueueList */
#include "los_sec_pri.h"      /* OsSIdGetDataCB / OsQueueSecCreateOut */
#include "los_box_pri.h"      /* OsGetCurBoxCB (box-context create routing) */
#endif
#include "los_task_pri.h"
#include "securec.h"
#include "los_config.h"
#include "los_debug.h"
#include "los_hook.h"
#include "los_interrupt.h"
#include "los_membox.h"
#include "los_memory.h"
#include "los_sched.h"
#include "los_sched_pri.h"
#include "los_mp_pri.h"
#include "los_percpu_pri.h"
#include <stdint.h>


#if (LOSCFG_BASE_IPC_QUEUE == 1)

#if !defined(LOSCFG_QUEUE_DYNAMIC_ALLOCATION) && (LOSCFG_QUEUE_STATIC_ALLOCATION != 1)
#error "MUST choose one mode for creating a queue!"
#endif

LITE_OS_SEC_BSS LosQueueCB *g_allQueue = NULL ;
LITE_OS_SEC_BSS LOS_DL_LIST g_freeQueueList;
#ifdef LOSCFG_BOX
/* Per-box queue free lists, one head per box over the box resource arrays
 * (same placement as the mux/sem box lists). */
LITE_OS_SEC_BSS LOS_DL_LIST g_boxFreeQueueList[LOSCFG_BASE_BOX_MAX_LIMIT];
/* Kernel queue entity array, registered with the SID module under
 * LOS_KERNEL_SID_HEAD (same shape as g_kernelMuxEntityArr in los_mux.c):
 * one EntityObj per kernel-table queue CB, parallel to g_allQueue. The
 * static queue pool is not covered - under BOX a static CB cannot pass the
 * create exit's OsSIdGenObj bind, so that path does not exist. */
LITE_OS_SEC_BSS STATIC EntityObj g_kernelQueEntityArr[LOSCFG_BASE_IPC_QUEUE_LIMIT];
#endif

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
LITE_OS_SEC_BSS LosQueueCB *g_staticQueue = NULL ;
LITE_OS_SEC_BSS LOS_DL_LIST g_freeStaticQueueList;
#endif

/*
 * IPC security gates. Open state (LOSCFG_BOX): a box caller addresses a
 * queue by its SID handle, and queue keeps TWO CB pools - the kernel table
 * plus the per-box pools (g_boxFreeQueueList over boxResQueList) - so the
 * handle cannot normalize to a kernel index the way a single-pool module
 * could. Every entry instead resolves the handle into the owning pool's CB
 * pointer up front (OsQueueResolve, the lock-free resolver shape refs calls
 * OsQueueGetCB) and the body runs on the CB; the create exit
 * (OsQueueCreateOut) binds the sId through OsSIdGenObj, which draws a slot
 * from the current context's SID pool and binds that pool's entity array.
 * The box create arm draws from the owning box's pool
 * (OsGetQueueFreeList); the kernel list serves kernel/interrupt contexts.
 * Closed state: the resolve gate collapses to the baseline bound check +
 * GET_QUEUE_HANDLE and CreateOut writes back the pool index, so a BOX=off
 * build keeps the baseline flow.
 */
#ifdef LOSCFG_BOX
STATIC INLINE UINT32 OsQueueResolve(UINT32 queueID, LosQueueCB **queueCB)
{
    return OsSIdGetDataCB(queueID, (UINT32)LOS_SID_TYPE_QUE, (VOID **)queueCB);
}

STATIC INLINE UINT32 OsQueueCreateOut(UINT32 *queueID, LosQueueCB *queueCB, UINT8 *queueMem)
{
    LOS_DL_LIST *freeList = NULL;
#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    /* with static and dynamic allocation coexisting, a failed reclaim must
     * go back to the list the CB came from (mirrors LOS_QueueDelete) or the
     * CB drifts between the pools. Static-pool queues cannot exist under
     * BOX anyway - the create exit cannot bind a CB outside both pools. */
    BOOL isStatic = (queueCB->queueID >= (UINT32)LOSCFG_BASE_IPC_QUEUE_LIMIT);
    if (isStatic) {
        freeList = &g_freeStaticQueueList;
    }
#else
    BOOL isStatic = FALSE;
#endif
    if (freeList == NULL) {
        /* dynamic CBs: the owning list follows the running context - the
         * box pool for a box create, the kernel list otherwise */
        OsGetQueueFreeList((UINTPTR)OsGetCurBoxCB(), &freeList);
    }
    UINT32 ret = OsQueueSecCreateOut(queueID, (VOID *)queueCB, queueMem, freeList, isStatic);
    if (ret != LOS_OK) {
        OsQueueDbgUpdateHook(queueCB->queueID, NULL);
    }
    return ret;
}
#else
STATIC INLINE UINT32 OsQueueResolve(UINT32 queueID, LosQueueCB **queueCB)
{
    if (queueID >= OS_ALL_IPC_QUEUE_LIMIT) {
        return LOS_ERRNO_QUEUE_INVALID;
    }
    *queueCB = GET_QUEUE_HANDLE(queueID);
    return LOS_OK;
}

STATIC INLINE UINT32 OsQueueCreateOut(UINT32 *queueID, LosQueueCB *queueCB, UINT8 *queueMem)
{
    (VOID)queueMem;
    *queueID = queueCB->queueID;
    return LOS_OK;
}
#endif /* LOSCFG_BOX */

/**************************************************************************
 Function    : OsQueueInit
 Description : queue initial
 Input       : None
 Output      : None
 Return      : LOS_OK on success or error code on failure
**************************************************************************/
LITE_OS_SEC_TEXT_INIT UINT32 OsQueueInit(VOID)
{
    LosQueueCB *queueNode = NULL;
    UINT16 index;

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    LosQueueCB *queueNodeStatic = NULL;
#endif

    if (OS_ALL_IPC_QUEUE_LIMIT == 0) {
        return LOS_ERRNO_QUEUE_MAXNUM_ZERO;
    }

    g_allQueue = (LosQueueCB *)LOS_MemAlloc(m_aucSysMem0, LOSCFG_BASE_IPC_QUEUE_LIMIT * sizeof(LosQueueCB));
    if (g_allQueue == NULL) {
        return LOS_ERRNO_QUEUE_NO_MEMORY;
    }

    (VOID)memset_s(g_allQueue, LOSCFG_BASE_IPC_QUEUE_LIMIT * sizeof(LosQueueCB),
                   0, LOSCFG_BASE_IPC_QUEUE_LIMIT * sizeof(LosQueueCB));

    LOS_ListInit(&g_freeQueueList);
#ifdef LOSCFG_BOX
    /* Register the kernel queue entity array with the SID module under the
     * kernel SID head, so a kernel-table queue CB binds against the kernel
     * entity array (OsSIdModuleInit shape from OsMuxInit). */
    if (OsSIdModuleInit(LOS_SID_TYPE_QUE, 0, g_kernelQueEntityArr, LOSCFG_BASE_IPC_QUEUE_LIMIT,
                        LOS_KERNEL_SID_HEAD) != LOS_OK) {
        return LOS_ERRNO_QUEUE_NO_MEMORY;
    }
#endif
    for (index = 0; index < LOSCFG_BASE_IPC_QUEUE_LIMIT; index++) {
        queueNode = ((LosQueueCB *)g_allQueue) + index;
        queueNode->queueID = index;
        LOS_ListTailInsert(&g_freeQueueList, &queueNode->readWriteList[OS_QUEUE_WRITE]);
    }

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    g_staticQueue = (LosQueueCB *)LOS_MemAlloc(m_aucSysMem0, LOSCFG_BASE_IPC_STATIC_QUEUE_LIMIT * sizeof(LosQueueCB));
    if (g_staticQueue == NULL) {
        return LOS_ERRNO_QUEUE_NO_MEMORY;
    }

    (VOID)memset_s(g_staticQueue, LOSCFG_BASE_IPC_STATIC_QUEUE_LIMIT * sizeof(LosQueueCB),
                   0, LOSCFG_BASE_IPC_STATIC_QUEUE_LIMIT * sizeof(LosQueueCB));

    LOS_ListInit(&g_freeStaticQueueList);
    for (index = 0; index < LOSCFG_BASE_IPC_STATIC_QUEUE_LIMIT; index++) {
        queueNodeStatic = ((LosQueueCB *)g_staticQueue) + index;
        queueNodeStatic->queueID = index + LOSCFG_BASE_IPC_QUEUE_LIMIT;
        LOS_ListTailInsert(&g_freeStaticQueueList, &queueNodeStatic->readWriteList[OS_QUEUE_WRITE]);
    }
#endif

    if (OsQueueDbgInitHook() != LOS_OK) {
        return LOS_ERRNO_QUEUE_NO_MEMORY;
    }
    return LOS_OK;
}


static UINT32 OsQueueCreate(const CHAR *queueName,
                            UINT16 len,
                            UINT32 *queueID,
                            UINT8 *staticMem,
                            UINT32 flags,
                            UINT16 maxMsgSize)
{
    LosQueueCB *queueCB = NULL;
    UINT32 intSave;
    LOS_DL_LIST *unusedQueue = NULL;
    UINT8 *queue = NULL;
    UINT16 msgSize;

    (VOID)flags;

    if (queueID == NULL) {
        return LOS_ERRNO_QUEUE_CREAT_PTR_NULL;
    }

    if (maxMsgSize > (OS_NULL_SHORT - sizeof(UINT32))) {
        return LOS_ERRNO_QUEUE_SIZE_TOO_BIG;
    }

    if ((len == 0) || (maxMsgSize == 0)) {
        return LOS_ERRNO_QUEUE_PARA_ISZERO;
    }
    msgSize = maxMsgSize + sizeof(UINT32);

    /* Memory allocation is time-consuming, to shorten the time of disable interrupt,
       move the memory allocation to here. */
    if ((UINT32_MAX / msgSize) < len) {
        return LOS_ERRNO_QUEUE_SIZE_TOO_BIG;
    }

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    if (staticMem != NULL) {
        queue = staticMem;
        SCHEDULER_LOCK(intSave);

        if (LOS_ListEmpty(&g_freeStaticQueueList)) {
            SCHEDULER_UNLOCK(intSave);
            return LOS_ERRNO_QUEUE_CB_UNAVAILABLE;
        }
        unusedQueue = LOS_DL_LIST_FIRST(&(g_freeStaticQueueList));
    } else {
        queue = (UINT8 *)LOS_MemAlloc(m_aucSysMem0, (UINT32)len * msgSize);
        if (queue == NULL) {
            return LOS_ERRNO_QUEUE_CREATE_NO_MEMORY;
        }

        SCHEDULER_LOCK(intSave);
#ifdef LOSCFG_BOX
        /* box contexts draw from their own pool (LOSCFG_BOXx_QUEUE_LIMIT);
         * kernel/interrupt contexts keep the kernel list */
        LOS_DL_LIST *cbList = NULL;
        OsGetQueueFreeList((UINTPTR)OsGetCurBoxCB(), &cbList);
        if (LOS_ListEmpty(cbList)) {
            SCHEDULER_UNLOCK(intSave);
            (VOID)LOS_MemFree(m_aucSysMem0, queue);
            OsQueueCheckHook();
            return LOS_ERRNO_QUEUE_CB_UNAVAILABLE;
        }
        unusedQueue = LOS_DL_LIST_FIRST(cbList);
#else
        if (LOS_ListEmpty(&g_freeQueueList)) {
            SCHEDULER_UNLOCK(intSave);
            (VOID)LOS_MemFree(m_aucSysMem0, queue);
            OsQueueCheckHook();
            return LOS_ERRNO_QUEUE_CB_UNAVAILABLE;
        }
        unusedQueue = LOS_DL_LIST_FIRST(&(g_freeQueueList));
#endif
    }
#else
    (void)staticMem;

    queue = (UINT8 *)LOS_MemAlloc(m_aucSysMem0, (UINT32)len * msgSize);
    if (queue == NULL) {
        return LOS_ERRNO_QUEUE_CREATE_NO_MEMORY;
    }

    SCHEDULER_LOCK(intSave);
#ifdef LOSCFG_BOX
    /* box contexts draw from their own pool; kernel/interrupt contexts keep
     * the kernel list */
    {
        LOS_DL_LIST *cbList = NULL;
        OsGetQueueFreeList((UINTPTR)OsGetCurBoxCB(), &cbList);
        if (LOS_ListEmpty(cbList)) {
            SCHEDULER_UNLOCK(intSave);
            (VOID)LOS_MemFree(m_aucSysMem0, queue);
            return LOS_ERRNO_QUEUE_CB_UNAVAILABLE;
        }
        unusedQueue = LOS_DL_LIST_FIRST(cbList);
    }
#else
    if (LOS_ListEmpty(&g_freeQueueList)) {
        SCHEDULER_UNLOCK(intSave);
        (VOID)LOS_MemFree(m_aucSysMem0, queue);
        return LOS_ERRNO_QUEUE_CB_UNAVAILABLE;
    }
    unusedQueue = LOS_DL_LIST_FIRST(&(g_freeQueueList));
#endif
#endif

    LOS_ListDelete(unusedQueue);
    queueCB = (GET_QUEUE_LIST(unusedQueue));
    queueCB->queueName = (UINT8 *)queueName; // The name can be null
    queueCB->queueLen = len;
    queueCB->queueSize = msgSize;
    queueCB->queue = queue;
    queueCB->queueState = OS_QUEUE_INUSED;
#ifdef LOSCFG_BOX
    /* Dynamic allocation reached this far, so the delete path frees the
     * message memory (the LOS_QueueCreateWithSId port sets STATIC instead). */
    queueCB->queueMemType = OS_QUEUE_ALLOC_DYNAMIC;
#endif
    queueCB->readWriteableCnt[OS_QUEUE_READ] = 0;
    queueCB->readWriteableCnt[OS_QUEUE_WRITE] = len;
    queueCB->queueHead = 0;
    queueCB->queueTail = 0;
    LOS_ListInit(&queueCB->readWriteList[OS_QUEUE_READ]);
    LOS_ListInit(&queueCB->readWriteList[OS_QUEUE_WRITE]);
    LOS_ListInit(&queueCB->memList);
    OsQueueDbgUpdateHook(queueCB->queueID, OsCurrTaskGet()->taskEntry);
    SCHEDULER_UNLOCK(intSave);

    /* SID-bound handle out (the baseline writes the pool index) */
    {
        UINT32 outRet = OsQueueCreateOut(queueID, queueCB, queue);
        if (outRet != LOS_OK) {
            return outRet;
        }
    }

    OsHookCall(LOS_HOOK_TYPE_QUEUE_CREATE, queueCB);

    return LOS_OK;
}

/*****************************************************************************
 Function    : LOS_QueueCreateStatic
 Description : Create a queue use static menory
 Input       : queueName  --- Queue name, less than 4 characters
             : len        --- Queue length
             : queueMem   --- Queue static memory for data storage
             : flags      --- Queue type, FIFO or PRIO
             : maxMsgSize --- Maximum message size in byte
 Output      : queueID    --- Queue ID
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
LITE_OS_SEC_TEXT_INIT UINT32 LOS_QueueCreateStatic(const CHAR *queueName,
                                                   UINT16 len,
                                                   UINT32 *queueID,
                                                   UINT32 flags,
                                                   UINT16 maxMsgSize,
                                                   VOID *queueMem,
                                                   UINT16 memSize)
{
    UINT32 ret;
    UINT32 msgSize;

    (VOID)flags;

    if (queueMem == NULL) {
        return LOS_ERRNO_QUEUE_CREAT_PTR_NULL;
    }

    msgSize = (UINT32)maxMsgSize + (UINT32)sizeof(UINT32);
    if ((memSize / msgSize) < len) {
        return LOS_ERRNO_QUEUE_CREATE_NO_MEMORY;
    }

    ret = OsQueueCreate(queueName, len, queueID, (UINT8 *)queueMem, 0, maxMsgSize);
    return ret;
}
#endif

/*****************************************************************************
 Function    : LOS_QueueCreate
 Description : Create a queue
 Input       : queueName  --- Queue name, less than 4 characters
             : len        --- Queue length
             : flags      --- Queue type, FIFO or PRIO
             : maxMsgSize --- Maximum message size in byte
 Output      : queueID    --- Queue ID
 Return      : LOS_OK on success or error code on failure
  *****************************************************************************/
#ifdef LOSCFG_QUEUE_DYNAMIC_ALLOCATION
LITE_OS_SEC_TEXT_INIT UINT32 LOS_QueueCreate(const CHAR *queueName,
                                             UINT16 len,
                                             UINT32 *queueID,
                                             UINT32 flags,
                                             UINT16 maxMsgSize)
{
    UINT32 ret;

    (VOID)flags;

    ret = OsQueueCreate(queueName, len, queueID, NULL, 0, maxMsgSize);
    return ret;
}
#endif

static INLINE LITE_OS_SEC_TEXT UINT32 OsQueueReadParameterCheck(const LosQueueCB *queueCB, VOID *bufferAddr,
                                                                UINT32 *bufferSize, UINT32 timeOut)
{
    if ((bufferAddr == NULL) || (bufferSize == NULL)) {
        return LOS_ERRNO_QUEUE_READ_PTR_NULL;
    }

    OsQueueDbgTimeUpdateHook(queueCB->queueID);

    if (*bufferSize == 0) {
        return LOS_ERRNO_QUEUE_READSIZE_ISZERO;
    }

    if (timeOut != LOS_NO_WAIT) {
        if (OS_INT_ACTIVE) {
            return LOS_ERRNO_QUEUE_READ_IN_INTERRUPT;
        }
    }
    return LOS_OK;
}

static INLINE LITE_OS_SEC_TEXT UINT32 OsQueueWriteParameterCheck(const LosQueueCB *queueCB, VOID *bufferAddr,
                                                                 UINT32 *bufferSize, UINT32 timeOut)
{
    if (bufferAddr == NULL) {
        return LOS_ERRNO_QUEUE_WRITE_PTR_NULL;
    }

    OsQueueDbgTimeUpdateHook(queueCB->queueID);

    if (*bufferSize == 0) {
        return LOS_ERRNO_QUEUE_WRITESIZE_ISZERO;
    }

    if (timeOut != LOS_NO_WAIT) {
        if (OS_INT_ACTIVE) {
            return LOS_ERRNO_QUEUE_WRITE_IN_INTERRUPT;
        }
    }
    return LOS_OK;
}

static INLINE VOID OsQueueBufferOperate(LosQueueCB *queueCB, UINT32 operateType,
                                                                VOID *bufferAddr, UINT32 *bufferSize)
{
    UINT8 *queueNode = NULL;
    UINT32 msgDataSize;
    UINT16 queuePosition;
    errno_t rc;

    /* get the queue position */
    switch (OS_QUEUE_OPERATE_GET(operateType)) {
        case OS_QUEUE_READ_HEAD:
            queuePosition = queueCB->queueHead;
            ((queueCB->queueHead + 1) == queueCB->queueLen) ? (queueCB->queueHead = 0) : (queueCB->queueHead++);
            break;

        case OS_QUEUE_WRITE_HEAD:
            (queueCB->queueHead == 0) ? (queueCB->queueHead = (queueCB->queueLen - 1)) : (--queueCB->queueHead);
            queuePosition = queueCB->queueHead;
            break;

        case OS_QUEUE_WRITE_TAIL:
            queuePosition = queueCB->queueTail;
            ((queueCB->queueTail + 1) == queueCB->queueLen) ? (queueCB->queueTail = 0) : (queueCB->queueTail++);
            break;

        default:
            PRINT_ERR("invalid queue operate type!\n");
            return;
    }

    queueNode = &(queueCB->queue[(queuePosition * (queueCB->queueSize))]);

    if (OS_QUEUE_IS_POINT(operateType)) {
        if (OS_QUEUE_IS_READ(operateType)) {
            *(UINTPTR *)bufferAddr = *(UINTPTR *)(VOID *)queueNode;
        } else {
            *(UINTPTR *)(VOID *)queueNode = *(UINTPTR *)bufferAddr;
        }
    } else {
        if (OS_QUEUE_IS_READ(operateType)) {
            msgDataSize = *((UINT32 *)(UINTPTR)((queueNode + queueCB->queueSize) - sizeof(UINT32)));
            msgDataSize = (*bufferSize < msgDataSize) ? *bufferSize : msgDataSize;
            rc = memcpy_s((VOID *)bufferAddr, *bufferSize, (VOID *)queueNode, msgDataSize);
            if (rc != EOK) {
                PRINT_ERR("%s[%d] memcpy failed, error type = %u\n", __FUNCTION__, __LINE__, rc);
                return;
            }

            *bufferSize = msgDataSize;
        } else {
            *((UINT32 *)(UINTPTR)((queueNode + queueCB->queueSize) - sizeof(UINT32))) = *bufferSize;
            rc = memcpy_s((VOID *)queueNode, queueCB->queueSize, (VOID *)bufferAddr, *bufferSize);
            if (rc != EOK) {
                PRINT_ERR("%s[%d] memcpy failed, error type = %u\n", __FUNCTION__, __LINE__, rc);
                return;
            }
        }
    }
}

static INLINE UINT32 OsQueueOperateParamCheck(const LosQueueCB *queueCB, UINT32 operateType, const UINT32 *bufferSize)
{
    if (queueCB->queueState == OS_QUEUE_UNUSED) {
        return LOS_ERRNO_QUEUE_NOT_CREATE;
    }

    if (OS_QUEUE_IS_WRITE(operateType) && (*bufferSize > (queueCB->queueSize - sizeof(UINT32)))) {
        return LOS_ERRNO_QUEUE_WRITE_SIZE_TOO_BIG;
    }

    if (*bufferSize >= SECUREC_MEM_MAX_LEN) {
        return LOS_ERRNO_QUEUE_BUFFER_SIZE_TOO_BIG;
    }

    return LOS_OK;
}

UINT32 OsQueueOperate(LosQueueCB *queueCB, UINT32 operateType, VOID *bufferAddr, UINT32 *bufferSize, UINT32 timeOut)
{
    LosTaskCB *resumedTask = NULL;
    UINT32 ret;
    UINT32 readWrite = OS_QUEUE_READ_WRITE_GET(operateType);
    UINT32 readWriteTmp = !readWrite;

    UINT32 intSave;
    SCHEDULER_LOCK(intSave);

    ret = OsQueueOperateParamCheck(queueCB, operateType, bufferSize);
    if (ret != LOS_OK) {
        goto QUEUE_END;
    }

    if (queueCB->readWriteableCnt[readWrite] == 0) {
        if (timeOut == LOS_NO_WAIT) {
            ret = OS_QUEUE_IS_READ(operateType) ? LOS_ERRNO_QUEUE_ISEMPTY : LOS_ERRNO_QUEUE_ISFULL;
            goto QUEUE_END;
        }

        if (!OsPreemptableInSched()) {
            ret = LOS_ERRNO_QUEUE_PEND_IN_LOCK;
            goto QUEUE_END;
        }

        LosTaskCB *runTsk = OsCurrTaskGet();
        OsSchedWait(OsCurrTaskGet(), &queueCB->readWriteList[readWrite], timeOut);
        SCHEDULER_UNLOCK(intSave);

        SCHEDULER_LOCK(intSave);
        if (runTsk->taskStatus & OS_TASK_STATUS_TIMEOUT) {
            runTsk->taskStatus &= ~OS_TASK_STATUS_TIMEOUT;
            ret = LOS_ERRNO_QUEUE_TIMEOUT;
            goto QUEUE_END;
        }
    } else {
        queueCB->readWriteableCnt[readWrite]--;
    }

    OsQueueBufferOperate(queueCB, operateType, bufferAddr, bufferSize);

    if (!LOS_ListEmpty(&queueCB->readWriteList[readWriteTmp])) {
        resumedTask = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&queueCB->readWriteList[readWriteTmp]));
        OsSchedWake(resumedTask);
        SCHEDULER_UNLOCK(intSave);
        LOS_MpSchedule(OS_MP_CPU_ALL);
        LOS_Schedule();
        return LOS_OK;
    } else {
        queueCB->readWriteableCnt[readWriteTmp]++;
    }

QUEUE_END:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

LITE_OS_SEC_TEXT UINT32 LOS_QueueReadCopy(UINT32 queueID,
                                          VOID *bufferAddr,
                                          UINT32 *bufferSize,
                                          UINT32 timeOut)
{
    UINT32 ret;
    UINT32 operateType;
    LosQueueCB *queueCB = NULL;

    ret = OsQueueResolve(queueID, &queueCB);
    if (ret != LOS_OK) {
        return LOS_ERRNO_QUEUE_INVALID;
    }

    ret = OsQueueReadParameterCheck(queueCB, bufferAddr, bufferSize, timeOut);
    if (ret != LOS_OK) {
        return ret;
    }

    operateType = OS_QUEUE_OPERATE_TYPE(OS_QUEUE_READ, OS_QUEUE_HEAD, OS_QUEUE_NOT_POINT);

    OsHookCall(LOS_HOOK_TYPE_QUEUE_READ_COPY, queueCB, operateType, *bufferSize, timeOut);

    return OsQueueOperate(queueCB, operateType, bufferAddr, bufferSize, timeOut);
}

LITE_OS_SEC_TEXT UINT32 LOS_QueueWriteHeadCopy(UINT32 queueID,
                                               VOID *bufferAddr,
                                               UINT32 bufferSize,
                                               UINT32 timeOut)
{
    UINT32 ret;
    UINT32 operateType;
    LosQueueCB *queueCB = NULL;

    ret = OsQueueResolve(queueID, &queueCB);
    if (ret != LOS_OK) {
        return LOS_ERRNO_QUEUE_INVALID;
    }

    ret = OsQueueWriteParameterCheck(queueCB, bufferAddr, &bufferSize, timeOut);
    if (ret != LOS_OK) {
        return ret;
    }

    operateType = OS_QUEUE_OPERATE_TYPE(OS_QUEUE_WRITE, OS_QUEUE_HEAD, OS_QUEUE_NOT_POINT);
    return OsQueueOperate(queueCB, operateType, bufferAddr, &bufferSize, timeOut);
}

LITE_OS_SEC_TEXT UINT32 LOS_QueueWriteCopy(UINT32 queueID,
                                           VOID *bufferAddr,
                                           UINT32 bufferSize,
                                           UINT32 timeOut)
{
    UINT32 ret;
    UINT32 operateType;
    LosQueueCB *queueCB = NULL;

    ret = OsQueueResolve(queueID, &queueCB);
    if (ret != LOS_OK) {
        return LOS_ERRNO_QUEUE_INVALID;
    }

    ret = OsQueueWriteParameterCheck(queueCB, bufferAddr, &bufferSize, timeOut);
    if (ret != LOS_OK) {
        return ret;
    }

    operateType = OS_QUEUE_OPERATE_TYPE(OS_QUEUE_WRITE, OS_QUEUE_TAIL, OS_QUEUE_NOT_POINT);

    OsHookCall(LOS_HOOK_TYPE_QUEUE_WRITE_COPY, queueCB, operateType, bufferSize, timeOut);

    return OsQueueOperate(queueCB, operateType, bufferAddr, &bufferSize, timeOut);
}

LITE_OS_SEC_TEXT UINT32 LOS_QueueRead(UINT32 queueID, VOID *bufferAddr, UINT32 bufferSize, UINT32 timeOut)
{
    UINT32 ret;
    UINT32 operateType;
    LosQueueCB *queueCB = NULL;

    /* SID handle -> pool index in place (no-op with BOX off) */
    ret = OsQueueResolve(queueID, &queueCB);
    if (ret != LOS_OK) {
        return LOS_ERRNO_QUEUE_INVALID;
    }

    ret = OsQueueReadParameterCheck(queueCB, bufferAddr, &bufferSize, timeOut);
    if (ret != LOS_OK) {
        return ret;
    }

    operateType = OS_QUEUE_OPERATE_TYPE(OS_QUEUE_READ, OS_QUEUE_HEAD, OS_QUEUE_POINT);

    OsHookCall(LOS_HOOK_TYPE_QUEUE_READ, queueCB, operateType, bufferSize, timeOut);

    return OsQueueOperate(queueCB, operateType, bufferAddr, &bufferSize, timeOut);
}

LITE_OS_SEC_TEXT UINT32 LOS_QueueWrite(UINT32 queueID, VOID *bufferAddr, UINT32 bufferSize, UINT32 timeOut)
{
    UINT32 ret;
    UINT32 operateType;
    LosQueueCB *queueCB = NULL;
    UINT32 size = sizeof(UINT32 *);
    (VOID)bufferSize;

    /* SID handle -> pool index in place (no-op with BOX off) */
    ret = OsQueueResolve(queueID, &queueCB);
    if (ret != LOS_OK) {
        return LOS_ERRNO_QUEUE_INVALID;
    }

    ret = OsQueueWriteParameterCheck(queueCB, bufferAddr, &size, timeOut);
    if (ret != LOS_OK) {
        return ret;
    }

    operateType = OS_QUEUE_OPERATE_TYPE(OS_QUEUE_WRITE, OS_QUEUE_TAIL, OS_QUEUE_POINT);

    OsHookCall(LOS_HOOK_TYPE_QUEUE_WRITE, queueCB, operateType, size, timeOut);

    return OsQueueOperate(queueCB, operateType, &bufferAddr, &size, timeOut);
}

LITE_OS_SEC_TEXT UINT32 LOS_QueueWriteHead(UINT32 queueID,
                                           VOID *bufferAddr,
                                           UINT32 bufferSize,
                                           UINT32 timeOut)
{
    UINT32 size = sizeof(UINT32 *);
    (VOID)bufferSize;

    if (bufferAddr == NULL) {
        return LOS_ERRNO_QUEUE_WRITE_PTR_NULL;
    }

    return LOS_QueueWriteHeadCopy(queueID, &bufferAddr, size, timeOut);
}

/*****************************************************************************
 Function    : OsQueueMailAlloc
 Description : Mail allocate memory
 Input       : queueID   --- QueueID
             : mailPool  --- MailPool
             : timeOut   --- TimeOut
 Output      : None
 Return      : mem:pointer if success otherwise NULL
 *****************************************************************************/
LITE_OS_SEC_TEXT VOID *OsQueueMailAlloc(UINT32 queueID, VOID *mailPool, UINT32 timeOut)
{
    VOID *mem = (VOID *)NULL;
    UINT32 intSave;
    LosQueueCB *queueCB = (LosQueueCB *)NULL;
    LosTaskCB *runTsk = (LosTaskCB *)NULL;

    if (OsQueueResolve(queueID, &queueCB) != LOS_OK) {
        return NULL;
    }

    if (mailPool == NULL) {
        return NULL;
    }

    if (timeOut != LOS_NO_WAIT) {
        if (OS_INT_ACTIVE) {
            return NULL;
        }
    }

    SCHEDULER_LOCK(intSave);
    OsQueueDbgTimeUpdateHook(queueCB->queueID);
    if (queueCB->queueState == OS_QUEUE_UNUSED) {
        goto END;
    }

    mem = LOS_MemboxAlloc(mailPool);
    if (mem == NULL) {
        if (timeOut == LOS_NO_WAIT) {
            goto END;
        }

        runTsk = OsCurrTaskGet();
        OsSchedWait(OsCurrTaskGet(), &queueCB->memList, timeOut);
        SCHEDULER_UNLOCK(intSave);

        SCHEDULER_LOCK(intSave);
        if (runTsk->taskStatus & OS_TASK_STATUS_TIMEOUT) {
            runTsk->taskStatus &= (~OS_TASK_STATUS_TIMEOUT);
            goto END;
        } else {
            /* When enters the current branch, means the current task already got an available membox,
             * so the runTsk->msg can not be NULL.
             */
            mem = runTsk->msg;
            runTsk->msg = NULL;
        }
    }

END:
    SCHEDULER_UNLOCK(intSave);
    return mem;
}

/*****************************************************************************
 Function    : OsQueueMailFree
 Description : Mail free memory
 Input       : queueID   --- QueueID
             : mailPool  --- MailPool
             : mailMem   --- MailMem
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT UINT32 OsQueueMailFree(UINT32 queueID, VOID *mailPool, VOID *mailMem)
{
    UINT32 intSave;
    LosQueueCB *queueCB = (LosQueueCB *)NULL;
    LosTaskCB *resumedTask = (LosTaskCB *)NULL;

    if (OsQueueResolve(queueID, &queueCB) != LOS_OK) {
        return LOS_ERRNO_QUEUE_MAIL_HANDLE_INVALID;
    }

    if (mailPool == NULL) {
        return LOS_ERRNO_QUEUE_MAIL_PTR_INVALID;
    }

    SCHEDULER_LOCK(intSave);
    OsQueueDbgTimeUpdateHook(queueCB->queueID);
    if (queueCB->queueState == OS_QUEUE_UNUSED) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_QUEUE_NOT_CREATE;
    }

    OsQueueDbgTimeUpdateHook(queueCB->queueID);

    if (!LOS_ListEmpty(&queueCB->memList)) {
        resumedTask = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&queueCB->memList));
        /* When enter this branch, it means the resumed task can
         * get an available mailMem.
         */
        resumedTask->msg = mailMem;
        OsSchedWake(resumedTask);
        SCHEDULER_UNLOCK(intSave);
        LOS_MpSchedule(OS_MP_CPU_ALL);
        LOS_Schedule();
    } else {
        /* No task waiting for the mailMem, so free it. */
        if (LOS_MemboxFree(mailPool, mailMem)) {
            SCHEDULER_UNLOCK(intSave);
            return LOS_ERRNO_QUEUE_MAIL_FREE_ERROR;
        }
        SCHEDULER_UNLOCK(intSave);
    }

    return LOS_OK;
}

/*****************************************************************************
 Function    : LOS_QueueDelete
 Description : Delete a queue
 Input       : queueID   --- QueueID
 Output      : None
 Return      : LOS_OK on success or error code on failure
 *****************************************************************************/
LITE_OS_SEC_TEXT_INIT UINT32 LOS_QueueDelete(UINT32 queueID)
{
    LosQueueCB *queueCB = NULL;
    UINT8 *queue = NULL;
    UINT32 intSave;
    UINT32 ret;

    if (OsQueueResolve(queueID, &queueCB) != LOS_OK) {
        return LOS_ERRNO_QUEUE_NOT_FOUND;
    }

    SCHEDULER_LOCK(intSave);
    if (queueCB->queueState == OS_QUEUE_UNUSED) {
        ret = LOS_ERRNO_QUEUE_NOT_CREATE;
        goto QUEUE_END;
    }

    if (!LOS_ListEmpty(&queueCB->readWriteList[OS_QUEUE_READ])) {
        ret = LOS_ERRNO_QUEUE_IN_TSKUSE;
        goto QUEUE_END;
    }

    if (!LOS_ListEmpty(&queueCB->readWriteList[OS_QUEUE_WRITE])) {
        ret = LOS_ERRNO_QUEUE_IN_TSKUSE;
        goto QUEUE_END;
    }

    if (!LOS_ListEmpty(&queueCB->memList)) {
        ret = LOS_ERRNO_QUEUE_IN_TSKUSE;
        goto QUEUE_END;
    }

    if ((queueCB->readWriteableCnt[OS_QUEUE_WRITE] + queueCB->readWriteableCnt[OS_QUEUE_READ]) !=
        queueCB->queueLen) {
        ret = LOS_ERRNO_QUEUE_IN_TSKWRITE;
        goto QUEUE_END;
    }

    queue = queueCB->queue;
    queueCB->queue = (UINT8 *)NULL;
    queueCB->queueName = (UINT8 *)NULL;
    queueCB->queueState = OS_QUEUE_UNUSED;

#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1) && !defined(LOSCFG_BOX)
    if (queueID >= LOSCFG_BASE_IPC_QUEUE_LIMIT && queueID < OS_ALL_IPC_QUEUE_LIMIT) {
        LOS_ListAdd(&g_freeStaticQueueList, &queueCB->readWriteList[OS_QUEUE_WRITE]);
        SCHEDULER_UNLOCK(intSave);
        OsQueueDbgUpdateHook(queueCB->queueID, NULL);
        return LOS_OK;
    }
#endif
#ifdef LOSCFG_BOX
    /* recycle to the owning pool: a box queue goes back to its box free
     * list, a kernel queue to the kernel list. Static-pool queues cannot
     * exist under BOX - the create exit cannot bind a CB that sits outside
     * both pools (its index overruns the registered entity array, which
     * OsSIdBind rejects), so the static arm above is compiled out. */
    if (!OS_SID_BID_IS_KER(queueID)) {
        LOS_ListAdd(&g_boxFreeQueueList[OS_GET_SID_BOXID(queueID)], &queueCB->readWriteList[OS_QUEUE_WRITE]);
    } else {
        LOS_ListAdd(&g_freeQueueList, &queueCB->readWriteList[OS_QUEUE_WRITE]);
    }
    /* Unbind the SID middle layer: clears the entity (owner / moduleData) and
     * frees the pool slot, so this sId stops resolving and the slot can be
     * handed out again. Same position sem/mux/event take, same lock hold. */
    (VOID)OsSIdObjDel(queueID);
#else
    LOS_ListAdd(&g_freeQueueList, &queueCB->readWriteList[OS_QUEUE_WRITE]);
#endif
    SCHEDULER_UNLOCK(intSave);

    OsQueueDbgUpdateHook(queueCB->queueID, NULL);
    OsHookCall(LOS_HOOK_TYPE_QUEUE_DELETE, queueCB);

#ifdef LOSCFG_BOX
    /* refs delete tail: only the DYNAMIC flavor is heap-freed. The static
     * flavor is LOS_QueueCreateWithSId's caller-provided memory - freeing
     * that walks the heap with a foreign pointer and fails the whole delete
     * with LOS_NOK. (The vanilla static-queue arm above is compiled out
     * under BOX, so this branch is the only static flavor there is.) */
    if (queueCB->queueMemType == OS_QUEUE_ALLOC_DYNAMIC) {
        ret = LOS_MemFree(m_aucSysMem0, (VOID *)queue);
    } else {
        ret = LOS_OK;
    }
#else
    ret = LOS_MemFree(m_aucSysMem0, (VOID *)queue);
#endif
    return ret;

QUEUE_END:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

LITE_OS_SEC_TEXT_MINOR UINT32 LOS_QueueInfoGet(UINT32 queueID, QUEUE_INFO_S *queueInfo)
{
    UINT32 intSave;
    UINT32 ret = LOS_OK;
    LosQueueCB *queueCB = NULL;
    LosTaskCB *tskCB = NULL;

    if (queueInfo == NULL) {
        return LOS_ERRNO_QUEUE_PTR_NULL;
    }

    if (OsQueueResolve(queueID, &queueCB) != LOS_OK) {
        return LOS_ERRNO_QUEUE_INVALID;
    }

    (VOID)memset_s((VOID *)queueInfo, sizeof(QUEUE_INFO_S), 0, sizeof(QUEUE_INFO_S));
    SCHEDULER_LOCK(intSave);

    if (queueCB->queueState == OS_QUEUE_UNUSED) {
        ret = LOS_ERRNO_QUEUE_NOT_CREATE;
        goto QUEUE_END;
    }

    queueInfo->queueID = queueID;
    queueInfo->queueLen = queueCB->queueLen;
    queueInfo->queueSize = queueCB->queueSize;
    queueInfo->queueHead = queueCB->queueHead;
    queueInfo->queueTail = queueCB->queueTail;
    queueInfo->readableCnt = queueCB->readWriteableCnt[OS_QUEUE_READ];
    queueInfo->writableCnt = queueCB->readWriteableCnt[OS_QUEUE_WRITE];

    LOS_DL_LIST_FOR_EACH_ENTRY(tskCB, &queueCB->readWriteList[OS_QUEUE_READ], LosTaskCB, pendList) {
        queueInfo->waitReadTask[OS_WAIT_TASK_ID_TO_ARRAY_IDX(tskCB->taskId)] |=
            (1 << (tskCB->taskId & OS_WAIT_TASK_ARRAY_ELEMENT_MASK));
    }

    LOS_DL_LIST_FOR_EACH_ENTRY(tskCB, &queueCB->readWriteList[OS_QUEUE_WRITE], LosTaskCB, pendList) {
        queueInfo->waitWriteTask[OS_WAIT_TASK_ID_TO_ARRAY_IDX(tskCB->taskId)] |=
            (1 << (tskCB->taskId & OS_WAIT_TASK_ARRAY_ELEMENT_MASK));
    }

    LOS_DL_LIST_FOR_EACH_ENTRY(tskCB, &queueCB->memList, LosTaskCB, pendList) {
        queueInfo->waitMemTask[OS_WAIT_TASK_ID_TO_ARRAY_IDX(tskCB->taskId)] |=
            (1 << (tskCB->taskId & OS_WAIT_TASK_ARRAY_ELEMENT_MASK));
    }

QUEUE_END:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

LosQueueCB *OsGetQueueHandle(UINT32 queueID)
{
#if (LOSCFG_QUEUE_STATIC_ALLOCATION == 1)
    if (queueID >= LOSCFG_BASE_IPC_QUEUE_LIMIT && queueID < OS_ALL_IPC_QUEUE_LIMIT) {
        return (((LosQueueCB *)g_staticQueue) + (queueID - LOSCFG_BASE_IPC_QUEUE_LIMIT));
    }
#endif

    return (((LosQueueCB *)g_allQueue) + (queueID));
}

#endif /* (LOSCFG_BASE_IPC_QUEUE == 1) */


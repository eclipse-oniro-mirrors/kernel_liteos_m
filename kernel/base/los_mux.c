/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
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

#include "los_mux_pri.h"
#include "los_mux_debug_pri.h"
#include "los_bitmap.h"
#include "los_exc.h"
#include "los_spinlock.h"
#include "los_interrupt.h"
#include "los_sched_pri.h"
#include "los_mp_pri.h"
#include "los_percpu_pri.h"
#include "los_trace.h"
#include "los_memory.h"
#include "los_hook.h"
#ifdef LOSCFG_BOX
#include "los_box_pri.h"
#endif

#if (LOSCFG_BASE_IPC_MUX == 1)

LITE_OS_SEC_BSS LosMuxCB *g_osAllMux = NULL;
#ifdef LOSCFG_BOX
/* M5C: the unused list + the per-box free lists are shared with
 * los_mux_box.c (refs g_freeQueueList / g_boxFreeQueueList pattern). */
LOS_DL_LIST g_unusedMuxList;
LITE_OS_SEC_BSS LOS_DL_LIST g_boxFreeMuxList[LOSCFG_BASE_BOX_MAX_LIMIT];
/* kernel mux entity array registered with the SID module (refs
 * g_kernelMuxEntity, registered under LOS_KERNEL_SID_HEAD). */
LITE_OS_SEC_BSS STATIC EntityObj g_kernelMuxEntityArr[LOSCFG_BASE_IPC_MUX_LIMIT];
EntityObj *g_kernelMuxEntity = g_kernelMuxEntityArr;
#else
LITE_OS_SEC_BSS STATIC LOS_DL_LIST g_unusedMuxList;
#endif

STATIC INLINE VOID OsMuxNodeRecycle(LosMuxCB *muxNode)
{
    muxNode->muxStat = LOS_UNUSED;
    LOS_ListTailInsert(&g_unusedMuxList, &muxNode->muxList);
}

LITE_OS_SEC_TEXT UINT32 OsMuxInit(VOID)
{
    LosMuxCB *muxNode = NULL;
    UINT32 index;

    if (LOSCFG_BASE_IPC_MUX_LIMIT == 0) {
        return LOS_ERRNO_MUX_MAXNUM_ZERO;
    }

    g_osAllMux = (LosMuxCB *)LOS_MemAlloc(m_aucSysMem0, (LOSCFG_BASE_IPC_MUX_LIMIT * sizeof(LosMuxCB)));
    if (g_osAllMux == NULL) {
        return LOS_ERRNO_MUX_NO_MEMORY;
    }

    LOS_ListInit(&g_unusedMuxList);

#ifdef LOSCFG_BOX
    /* M5C (refs OsQueueInit pattern, refs los_mux.c OsMuxInit): register
     * the kernel mux entity array with the SID module under the kernel
     * SID head so kernel mux handles become sIds. Kernel mux creation
     * then consumes kernel SID pool slots 1:1. */
    OsSIdListInit();
    if (OsSIdModuleInit(LOS_SID_TYPE_MUX, 0, g_kernelMuxEntity, LOSCFG_BASE_IPC_MUX_LIMIT,
                        LOS_KERNEL_SID_HEAD) != LOS_OK) {
        return LOS_ERRNO_MUX_NO_MEMORY;
    }
#endif

    for (index = 0; index < LOSCFG_BASE_IPC_MUX_LIMIT; index++) {
        muxNode = g_osAllMux + index;
        muxNode->muxId = index;
        OsMuxNodeRecycle(muxNode);
    }

    if (OsMuxDbgInitHook() != LOS_OK) {
        return LOS_ERRNO_MUX_NO_MEMORY;
    }
    return LOS_OK;
}

LITE_OS_SEC_TEXT UINT32 LOS_MuxCreate(UINT32 *muxHandle)
{
    UINT32 intSave;
    LosMuxCB *muxCreated = NULL;
    LOS_DL_LIST *unusedMux = NULL;
#ifdef LOSCFG_BASE_IPC_MUX_CREATOR
    UINTPTR creator = ArchGetLr();
#endif

    if (muxHandle == NULL) {
        return LOS_ERRNO_MUX_PTR_NULL;
    }

    SCHEDULER_LOCK(intSave);
    if (LOS_ListEmpty(&g_unusedMuxList)) {
        SCHEDULER_UNLOCK(intSave);
        OsMutexCheckHook();
        return LOS_ERRNO_MUX_ALL_BUSY;
    }

    unusedMux = LOS_DL_LIST_FIRST(&g_unusedMuxList);
    LOS_ListDelete(unusedMux);
    muxCreated = LOS_DL_LIST_ENTRY(unusedMux, LosMuxCB, muxList);
    muxCreated->muxCount = 0;
    muxCreated->muxStat = LOS_USED;
    muxCreated->owner = NULL;
    muxCreated->priority = 0;
    LOS_ListInit(&muxCreated->muxList);
#ifdef LOSCFG_BOX
    /* M5C: bind a kernel-head sId to the CB (refs OsMuxCreate binds the
     * SID after taking the CB from the free list); muxId carries the sId
     * so OsMuxStateVerify's handle comparison keeps working. */
    {
        UINT32 newSId;
        UINT32 index = (UINT32)(muxCreated - g_osAllMux);
        if (OsSIdGenObj(LOS_SID_TYPE_MUX, index, (VOID *)muxCreated, &newSId) != LOS_OK) {
            LOS_ListTailInsert(&g_unusedMuxList, unusedMux);
            SCHEDULER_UNLOCK(intSave);
            return LOS_ERRNO_MUX_ALL_BUSY;
        }
        muxCreated->muxId = newSId;
        *muxHandle = newSId;
    }
#else
    *muxHandle = muxCreated->muxId;
#endif

    OsMuxDbgUpdateHook(muxCreated->muxId, OsCurrTaskGet()->taskEntry);
#ifdef LOSCFG_BASE_IPC_MUX_CREATOR
    OsMuxCreatorSet(muxCreated, creator);
#endif

    SCHEDULER_UNLOCK(intSave);

    LOS_TRACE(MUX_CREATE, muxCreated->muxId);
    OsHookCall(LOS_HOOK_TYPE_MUX_CREATE, muxCreated);
    return LOS_OK;
}

#ifdef LOSCFG_BASE_CORE_SYS_RES_CHECK
STATIC INLINE UINT32 OsMuxStateVerify(const LosMuxCB *muxCB, UINT32 muxHandle)
{
    if ((muxCB->muxId != muxHandle) || (muxCB->muxStat == LOS_UNUSED)) {
        return LOS_ERRNO_MUX_INVALID;
    }

    return LOS_OK;
}

STATIC INLINE VOID OsMuxIdUpdate(LosMuxCB *muxDeleted)
{
    /* BOX (refs-lacks note): with sId handles, muxId carries the sId, and
     * SET_MUX_ID would rebuild garbage out of it. Stale-handle detection
     * moves to the SID module: OsSIdObjDel unbinds and zeroes the SIdObj,
     * so a recycled sId fails the OsSIdObjGet sId comparison. */
#ifdef LOSCFG_BOX
    (VOID)muxDeleted;
#else
    muxDeleted->muxId = SET_MUX_ID(GET_MUX_COUNT(muxDeleted->muxId) + 1, GET_MUX_INDEX(muxDeleted->muxId));
#endif
}
#else
STATIC INLINE UINT32 OsMuxStateVerify(const LosMuxCB *muxCB, UINT32 muxHandle)
{
    (VOID)muxHandle;

    if (muxCB->muxStat == LOS_UNUSED) {
        return LOS_ERRNO_MUX_INVALID;
    }

    return LOS_OK;
}

STATIC INLINE VOID OsMuxIdUpdate(LosMuxCB *muxDeleted)
{
    (VOID)muxDeleted;
}
#endif

#ifdef LOSCFG_BOX
/* BOX: recycle to the owning free list (refs queue delete recycles into
 * the box free list, not the kernel one). */
STATIC INLINE VOID OsMuxNodeRecycleBySId(LosMuxCB *muxNode, UINT32 sId)
{
    muxNode->muxStat = LOS_UNUSED;
    if (OS_SID_BID_IS_KER(sId)) {
        LOS_ListTailInsert(&g_unusedMuxList, &muxNode->muxList);
    } else {
        LOS_ListTailInsert(&g_boxFreeMuxList[OS_GET_SID_BOXID(sId)], &muxNode->muxList);
    }
}
#endif

LITE_OS_SEC_TEXT UINT32 LOS_MuxDelete(UINT32 muxHandle)
{
    UINT32 intSave;
    LosMuxCB *muxDeleted = NULL;
    UINT32 ret;

#ifdef LOSCFG_BOX
    SCHEDULER_LOCK(intSave);
    /* resolve the sId (kernel-head or box) under the lock */
    if (OsMuxGetCBLocked(muxHandle, &muxDeleted) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_MUX_INVALID;
    }
    /* kernel-owned resources must not be destroyed by boxes (box heap
     * lock mux defense; refs OsSIdKernelResIntCheck position) */
    if (OsSIdKernelResIntCheck(muxHandle) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_MUX_INVALID;
    }
#else
    if (GET_MUX_INDEX(muxHandle) >= (UINT32)LOSCFG_BASE_IPC_MUX_LIMIT) {
        return LOS_ERRNO_MUX_INVALID;
    }

    muxDeleted = GET_MUX(muxHandle);

    SCHEDULER_LOCK(intSave);
#endif

    LOS_TRACE(MUX_DELETE, muxHandle, muxDeleted->muxStat, muxDeleted->muxCount,
        ((muxDeleted->owner == NULL) ? 0xFFFFFFFF : muxDeleted->owner->taskId));

    ret = OsMuxStateVerify(muxDeleted, muxHandle);
    if (ret != LOS_OK) {
        goto OUT;
    }

    if (!LOS_ListEmpty(&muxDeleted->muxList) || (muxDeleted->muxCount != 0)) {
        ret = LOS_ERRNO_MUX_PENDED;
        goto OUT;
    }
    OsMuxIdUpdate(muxDeleted);
#ifdef LOSCFG_BOX
    /* unbind the SID (frees the SIdObj back to its pool), then recycle
     * the CB to the owning free list */
    (VOID)OsSIdObjDel(muxHandle);
    OsMuxNodeRecycleBySId(muxDeleted, muxHandle);
#else
    OsMuxNodeRecycle(muxDeleted);
#endif

#ifdef LOSCFG_BASE_IPC_MUX_CREATOR
    OsMuxCreatorClear(muxDeleted);
#endif
    OsMuxDbgUpdateHook(muxDeleted->muxId, NULL);

 OUT:
    SCHEDULER_UNLOCK(intSave);
    OsHookCall(LOS_HOOK_TYPE_MUX_DELETE, muxDeleted);
    return ret;
}

LITE_OS_SEC_TEXT STATIC UINT32 OsMuxParaCheck(const LosMuxCB *muxCB, UINT32 muxHandle)
{
    UINT32 ret = OsMuxStateVerify(muxCB, muxHandle);
    if (ret != LOS_OK) {
        return ret;
    }

    OsMuxDbgTimeUpdateHook(muxCB->muxId);

    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_MUX_PEND_INTERR;
    }

    return LOS_OK;
}

LITE_OS_SEC_TEXT STATIC VOID OsMuxBitmapSet(const LosTaskCB *runTask, const MuxBaseCB *muxPended)
{
    if (GET_MUX_OWNER(muxPended)->priority > runTask->priority) {
        LOS_BitmapSet(&(GET_MUX_OWNER(muxPended)->priBitMap), GET_MUX_OWNER(muxPended)->priority);
        OsSchedPrioModify(GET_MUX_OWNER(muxPended), runTask->priority);
    }
}

LITE_OS_SEC_TEXT VOID OsMuxBitmapRestore(const LosTaskCB *runTask, LosTaskCB *owner)
{
    UINT16 bitMapPri;

    if (owner->priority >= runTask->priority) {
        bitMapPri = LOS_LowBitGet(owner->priBitMap);
        if (bitMapPri != LOS_INVALID_BIT_INDEX) {
            LOS_BitmapClr(&(owner->priBitMap), bitMapPri);
            OsSchedPrioModify(owner, bitMapPri);
        }
    } else {
        if (LOS_HighBitGet(owner->priBitMap) != runTask->priority) {
            LOS_BitmapClr(&(owner->priBitMap), runTask->priority);
        }
    }
}

#ifdef LOSCFG_MUTEX_WAITMODE_PRIO
LITE_OS_SEC_TEXT STATIC LOS_DL_LIST *OsMuxPendFindPosSub(const LosTaskCB *runTask, const MuxBaseCB *muxPended)
{
    LosTaskCB *pendedTask = NULL;
    LOS_DL_LIST *node = NULL;

    LOS_DL_LIST_FOR_EACH_ENTRY(pendedTask, &(muxPended->muxList), LosTaskCB, pendList) {
        if (pendedTask->priority < runTask->priority) {
            continue;
        } else if (pendedTask->priority > runTask->priority) {
            node = &pendedTask->pendList;
            break;
        } else {
            node = pendedTask->pendList.pstNext;
            break;
        }
    }

    return node;
}

LITE_OS_SEC_TEXT STATIC LOS_DL_LIST *OsMuxPendFindPos(const LosTaskCB *runTask, MuxBaseCB *muxPended)
{
    LOS_DL_LIST *node = NULL;
    LosTaskCB *pendedTask1 = NULL;
    LosTaskCB *pendedTask2 = NULL;

    if (LOS_ListEmpty(&muxPended->muxList)) {
        node = &muxPended->muxList;
    } else {
        pendedTask1 = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&muxPended->muxList));
        pendedTask2 = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_LAST(&muxPended->muxList));
        if ((pendedTask1 != NULL) && (pendedTask1->priority > runTask->priority)) {
            node = muxPended->muxList.pstNext;
        } else if ((pendedTask2 != NULL) && (pendedTask2->priority <= runTask->priority)) {
            node = &muxPended->muxList;
        } else {
            node = OsMuxPendFindPosSub(runTask, muxPended);
        }
    }
    return node;
}
#else
LITE_OS_SEC_TEXT STATIC LOS_DL_LIST *OsMuxPendFindPos(const LosTaskCB *runTask, MuxBaseCB *muxPended)
{
    (VOID)runTask;
    LOS_DL_LIST *node = NULL;
    node = &muxPended->muxList;
    return node;
}
#endif

LITE_OS_SEC_TEXT UINT32 OsMuxPendOp(LosTaskCB *runTask, MuxBaseCB *muxPended, UINT32 timeout)
{
    LOS_DL_LIST *node = NULL;
    UINT32 ret = LOS_OK;
    LosTaskCB *owner = GET_MUX_OWNER(muxPended);

    runTask->taskMux = (VOID *)muxPended;
    node = OsMuxPendFindPos(runTask, muxPended);

    OsSchedWait(runTask, node, timeout);

    if (runTask->taskStatus & OS_TASK_STATUS_TIMEOUT) {
        runTask->taskStatus &= ~OS_TASK_STATUS_TIMEOUT;
        ret = LOS_ERRNO_MUX_TIMEOUT;
    }

    if (timeout != LOS_WAIT_FOREVER) {
        OsMuxBitmapRestore(runTask, owner);
    }

    return ret;
}

LITE_OS_SEC_TEXT UINT32 LOS_MuxPend(UINT32 muxHandle, UINT32 timeout)
{
    UINT32 ret, intSave;
    LosMuxCB *muxPended = NULL;
    LosTaskCB *runTask = NULL;

#ifdef LOSCFG_BOX
    SCHEDULER_LOCK(intSave);
    /* resolve the sId under the lock (kernel-head or box sId) */
    if (OsMuxGetCBLocked(muxHandle, &muxPended) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_MUX_INVALID;
    }
#else
    if (GET_MUX_INDEX(muxHandle) >= (UINT32)LOSCFG_BASE_IPC_MUX_LIMIT) {
        return LOS_ERRNO_MUX_INVALID;
    }

    muxPended = GET_MUX(muxHandle);
    SCHEDULER_LOCK(intSave);
#endif

    LOS_TRACE(MUX_PEND, muxHandle, muxPended->muxCount,
        ((muxPended->owner == NULL) ? 0xFFFFFFFF : GET_MUX_OWNER(muxPended)->taskId), timeout);

    ret = OsMuxParaCheck(muxPended, muxHandle);
    if (ret != LOS_OK) {
        goto OUT_UNLOCK;
    }

    runTask = OsCurrTaskGet();
    if (runTask->taskFlags & OS_TASK_FLAG_SYSTEM) {
        PRINT_DEBUG("Warning: DO NOT recommend to use %s in system tasks.\n", __FUNCTION__);
        ret = LOS_ERRNO_MUX_PEND_IN_SYSTEM_TASK;
        goto OUT_UNLOCK;
    }

    if (!OsPreemptableInSched()) {
        PRINT_ERR("!!!LOS_ERRNO_MUX_PEND_IN_LOCK!!!\n");
        ret = LOS_ERRNO_MUX_PEND_IN_LOCK;
        goto OUT_UNLOCK;
    }

    if (muxPended->owner == runTask) {
        muxPended->muxCount++;
        goto OUT_UNLOCK;
    }

    if (muxPended->muxCount == 0) {
        muxPended->muxCount++;
        muxPended->owner = runTask;
        goto OUT_UNLOCK;
    }

    if (timeout == 0) {
        ret = LOS_ERRNO_MUX_UNAVAILABLE;
        goto OUT_UNLOCK;
    }

    OsMuxBitmapSet(runTask, (MuxBaseCB *)muxPended);
    ret = OsMuxPendOp(runTask, (MuxBaseCB *)muxPended, timeout);
    if (ret != LOS_OK) {
        goto OUT_UNLOCK;
    }

OUT_UNLOCK:
    SCHEDULER_UNLOCK(intSave);
    if (ret == LOS_OK) {
        OsHookCall(LOS_HOOK_TYPE_MUX_PEND, muxPended, timeout);
    }
    return ret;
}

LITE_OS_SEC_TEXT VOID OsMuxPostOpSub(LosTaskCB *runTask, MuxBaseCB *muxPosted)
{
    LosTaskCB *pendedTask = NULL;
    UINT16 bitMapPri;

    if (!LOS_ListEmpty(&muxPosted->muxList)) {
        bitMapPri = LOS_HighBitGet(runTask->priBitMap);
        LOS_DL_LIST_FOR_EACH_ENTRY(pendedTask, (&muxPosted->muxList), LosTaskCB, pendList) {
            if (bitMapPri != pendedTask->priority) {
                LOS_BitmapClr(&runTask->priBitMap, pendedTask->priority);
            }
        }
    }
    bitMapPri = LOS_LowBitGet(runTask->priBitMap);
    LOS_BitmapClr(&runTask->priBitMap, bitMapPri);
    OsSchedPrioModify(GET_MUX_OWNER(muxPosted), bitMapPri);
}

LITE_OS_SEC_TEXT UINT32 OsMuxPostOp(LosTaskCB *runTask, MuxBaseCB *muxPosted)
{
    LosTaskCB *resumedTask = NULL;

    if (LOS_ListEmpty(&muxPosted->muxList)) {
        muxPosted->owner = NULL;
        return MUX_NO_SCHEDULE;
    }

    resumedTask = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&(muxPosted->muxList)));
#ifdef LOSCFG_MUTEX_WAITMODE_PRIO
    if (resumedTask->priority > runTask->priority) {
        if (LOS_HighBitGet(runTask->priBitMap) != resumedTask->priority) {
            LOS_BitmapClr(&runTask->priBitMap, resumedTask->priority);
        }
    } else if (runTask->priBitMap != 0) {
        OsMuxPostOpSub(runTask, muxPosted);
    }
#else
    if (runTask->priBitMap != 0) {
        OsMuxPostOpSub(runTask, muxPosted);
    }
#endif

    muxPosted->muxCount = 1;
    muxPosted->owner = resumedTask;
    resumedTask->taskMux = NULL;

    OsSchedWake(resumedTask);

    return MUX_SCHEDULE;
}

LITE_OS_SEC_TEXT UINT32 LOS_MuxPost(UINT32 muxHandle)
{
    UINT32 ret;
    LosTaskCB *runTask = NULL;
    LosMuxCB *muxPosted = NULL;
    UINT32 intSave;

#ifdef LOSCFG_BOX
    SCHEDULER_LOCK(intSave);
    /* resolve the sId under the lock (kernel-head or box sId) */
    if (OsMuxGetCBLocked(muxHandle, &muxPosted) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_MUX_INVALID;
    }
#else
    if (GET_MUX_INDEX(muxHandle) >= (UINT32)LOSCFG_BASE_IPC_MUX_LIMIT) {
        return LOS_ERRNO_MUX_INVALID;
    }

    muxPosted = GET_MUX(muxHandle);
    SCHEDULER_LOCK(intSave);
#endif

    LOS_TRACE(MUX_POST, muxHandle, muxPosted->muxCount,
        ((muxPosted->owner == NULL) ? 0xFFFFFFFF : muxPosted->owner->taskId));

    ret = OsMuxParaCheck(muxPosted, muxHandle);
    if (ret != LOS_OK) {
        goto OUT_UNLOCK;
    }

    runTask = OsCurrTaskGet();
    if ((muxPosted->muxCount == 0) || (muxPosted->owner != runTask)) {
        ret = LOS_ERRNO_MUX_INVALID;
        goto OUT_UNLOCK;
    }

    if (--muxPosted->muxCount != 0) {
        goto OUT_UNLOCK;
    }

    ret = OsMuxPostOp(runTask, (MuxBaseCB *)muxPosted);
    SCHEDULER_UNLOCK(intSave);
    if (ret == MUX_SCHEDULE) {
        LOS_MpSchedule(OS_MP_CPU_ALL);
        LOS_Schedule();
    }
    OsHookCall(LOS_HOOK_TYPE_MUX_POST, muxPosted);
    return LOS_OK;

OUT_UNLOCK:
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

#ifdef LOSCFG_BASE_IPC_MUX_CREATOR
VOID OsMuxCreatorSet(LosMuxCB *mux, UINTPTR creator)
{
    mux->creator = creator;
}

VOID OsMuxCreatorClear(LosMuxCB *mux)
{
    mux->creator = 0;
}
#endif

#endif /* (LOSCFG_BASE_IPC_MUX == 1) */

/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted under the following conditions are met:
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
 * WHETHER IN CONTRACT, STRICT LIABILITY, TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "los_sem_pri.h"
#include "los_sem_debug_pri.h"
#include "los_task_pri.h"
#include "los_spinlock.h"
#include "los_interrupt.h"
#include "los_memory.h"
#include "los_sched_pri.h"
#include "los_mp_pri.h"
#include "los_percpu_pri.h"
#include "los_trace.h"
#include "los_exc.h"
#include "los_hook.h"
#ifdef LOSCFG_BOX
#include "los_box_pri.h"        /* OsGetCurBoxCB (sem entry seams) */
#endif

#if (LOSCFG_BASE_IPC_SEM == 1)

LITE_OS_SEC_BSS LosSemCB *g_osAllSem = NULL;
#ifdef LOSCFG_BOX
/* BOX (same placement as the mux lists in los_mux.c): the kernel unused list
 * goes non-STATIC so OsGetSemFreeList (los_sem_box.c) can route to it; the
 * per-box free lists live over the box resource arrays. */
LITE_OS_SEC_BSS LOS_DL_LIST g_unusedSemList;
LITE_OS_SEC_BSS LOS_DL_LIST g_boxFreeSemList[LOSCFG_BASE_BOX_MAX_LIMIT];
/* Kernel sem entity array, registered with the SID module under
 * LOS_KERNEL_SID_HEAD (same shape as g_kernelMuxEntityArr in los_mux.c):
 * one EntityObj per kernel-table sem CB, parallel to g_osAllSem. */
LITE_OS_SEC_BSS STATIC EntityObj g_kernelSemEntityArr[LOSCFG_BASE_IPC_SEM_LIMIT];
#else
LITE_OS_SEC_BSS STATIC LOS_DL_LIST g_unusedSemList;
#endif

/* refs OsSemNodeRecycle: the pool is a PARAMETER, so one helper serves the
 * kernel table and every box pool alike. (refs spells the parameter const;
 * LOS_ListTailInsert writes through it, so it is not const here.) */
STATIC INLINE VOID OsSemNodeRecycle(LosSemCB *semNode, LOS_DL_LIST *freeList)
{
    semNode->semStat = LOS_UNUSED;
    LOS_ListTailInsert(freeList, &semNode->semList);
}

LITE_OS_SEC_TEXT_INIT UINT32 OsSemInit(VOID)
{
    LosSemCB *semNode = NULL;
    UINT32 index;

    if (LOSCFG_BASE_IPC_SEM_LIMIT == 0) {
        return LOS_ERRNO_SEM_MAXNUM_ZERO;
    }

    g_osAllSem = (LosSemCB *)LOS_MemAlloc(m_aucSysMem0, (LOSCFG_BASE_IPC_SEM_LIMIT * sizeof(LosSemCB)));
    if (g_osAllSem == NULL) {
        return LOS_ERRNO_SEM_NO_MEMORY;
    }

    LOS_ListInit(&g_unusedSemList);

#ifdef LOSCFG_BOX
    /* Register the kernel sem entity array with the SID module under the
     * kernel SID head, so a kernel-table sem CB binds against the kernel
     * entity array (OsSIdModuleInit shape from OsMuxInit). */
    if (OsSIdModuleInit(LOS_SID_TYPE_SEM, 0, g_kernelSemEntityArr, LOSCFG_BASE_IPC_SEM_LIMIT,
                        LOS_KERNEL_SID_HEAD) != LOS_OK) {
        return LOS_ERRNO_SEM_NO_MEMORY;
    }
#endif

    for (index = 0; index < LOSCFG_BASE_IPC_SEM_LIMIT; index++) {
        semNode = g_osAllSem + index;
        semNode->semId = index;
        OsSemNodeRecycle(semNode, &g_unusedSemList);
    }

    if (OsSemDbgInitHook() != LOS_OK) {
        return LOS_ERRNO_SEM_NO_MEMORY;
    }
    return LOS_OK;
}

#ifndef LOSCFG_BOX
/* Baseline create core, upstream verbatim. Under BOX this function does not
 * exist at all: OsSemCreateInternal (los_sem_box.c) is the single refs-shaped
 * core for EVERY context and selects the pool inside itself. */
LITE_OS_SEC_TEXT_INIT STATIC UINT32 OsSemCreate(UINT16 count, UINT8 type, UINT32 *semHandle)
{
    UINT32 intSave;
    LosSemCB *semCreated = NULL;
    LOS_DL_LIST *unusedSem = NULL;

    if (semHandle == NULL) {
        return LOS_ERRNO_SEM_PTR_NULL;
    }

    SCHEDULER_LOCK(intSave);

    if (LOS_ListEmpty(&g_unusedSemList)) {
        SCHEDULER_UNLOCK(intSave);
        OsSemInfoGetFullDataHook();
        return LOS_ERRNO_SEM_ALL_BUSY;
    }

    unusedSem = LOS_DL_LIST_FIRST(&g_unusedSemList);
    LOS_ListDelete(unusedSem);
    semCreated = GET_SEM_LIST(unusedSem);
    semCreated->semStat = LOS_USED;
    semCreated->semType = type;
    semCreated->semCount = count;
    LOS_ListInit(&semCreated->semList);
    *semHandle = semCreated->semId;

    OsSemDbgUpdateHook(semCreated->semId, OsCurrTaskGet()->taskEntry, count);

    SCHEDULER_UNLOCK(intSave);

    LOS_TRACE(SEM_CREATE, semCreated->semId, type, count);
    OsHookCall(LOS_HOOK_TYPE_SEM_CREATE, semCreated);
    return LOS_OK;
}
#endif /* !LOSCFG_BOX */

LITE_OS_SEC_TEXT_INIT UINT32 LOS_SemCreate(UINT16 count, UINT32 *semHandle)
{
#ifdef LOSCFG_BOX
    return OsSemCreateInternal(count, semHandle, OS_SEM_COUNTING);
#else
    if (count > LOS_SEM_COUNT_MAX) {
        return LOS_ERRNO_SEM_OVERFLOW;
    }
    return OsSemCreate(count, OS_SEM_COUNTING, semHandle);
#endif
}

LITE_OS_SEC_TEXT_INIT UINT32 LOS_BinarySemCreate(UINT16 count, UINT32 *semHandle)
{
#ifdef LOSCFG_BOX
    return OsSemCreateInternal(count, semHandle, OS_SEM_BINARY);
#else
    if (count > OS_SEM_BINARY_COUNT_MAX) {
        return LOS_ERRNO_SEM_OVERFLOW;
    }
    return OsSemCreate(count, OS_SEM_BINARY, semHandle);
#endif
}

#ifdef LOSCFG_BASE_CORE_SYS_RES_CHECK
STATIC INLINE UINT32 OsSemStateVerify(UINT32 semId, const LosSemCB *semNode)
{
    if ((semNode->semStat == LOS_UNUSED) || (semNode->semId != semId)) {
        return LOS_ERRNO_SEM_INVALID;
    }
    return LOS_OK;
}

STATIC INLINE VOID OsSemIdUpdate(LosSemCB *semDeleted)
{
#ifdef LOSCFG_BOX
    /* With sId handles semId carries the sId, and SET_SEM_ID would rebuild
     * garbage out of it (GET_SEM_COUNT of an sId is meaningless). Stale-handle
     * detection belongs to the SID module instead: OsSIdObjDel unbinds on
     * delete, so a recycled handle fails the later resolve. Same shape as
     * OsMuxIdUpdate. */
    (VOID)semDeleted;
#else
    semDeleted->semId = SET_SEM_ID(GET_SEM_COUNT(semDeleted->semId) + 1, GET_SEM_INDEX(semDeleted->semId));
#endif
}
#else
STATIC INLINE UINT32 OsSemStateVerify(UINT32 semId, const LosSemCB *semNode)
{
    (VOID)semId;

    if (semNode->semStat == LOS_UNUSED) {
        return LOS_ERRNO_SEM_INVALID;
    }
    return LOS_OK;
}

STATIC INLINE VOID OsSemIdUpdate(LosSemCB *semDeleted)
{
    (VOID)semDeleted;
}
#endif

/* BOX entry shape (refs safe_ipc): the sId -> CB resolver is a plain,
 * lock-free OsSemGetCB (los_sem_box.c) and the scheduler lock belongs to the
 * CALLING entry - each entry takes the lock itself, resolves inside it, and a
 * failed resolve releases it right there. Sem keeps two CB pools (kernel plus
 * per-box), so a handle cannot be reduced to a kernel index the way a
 * single-pool module could; resolution goes through the SID module instead.
 *
 * The #ifdef pair lives per entry, not in a shared helper, because the two
 * worlds differ in lock POSITION and not only in the resolver: the baseline
 * keeps the upstream prologue verbatim (bound check and GET_SEM outside the
 * lock, LOS_TRACE ahead of the lock), whereas a box context must already hold
 * the lock before it can resolve. Folding both into one helper forces the
 * baseline arm through the helper's return-value contract - which grows the
 * BOX=off image - and handing the lock to the helper makes the pairing
 * asymmetric (locked inside, unlocked outside). */

LITE_OS_SEC_TEXT_INIT UINT32 LOS_SemDelete(UINT32 semHandle)
{
    UINT32 intSave;
    LosSemCB *semDeleted = NULL;
    UINT32 ret;
#ifdef LOSCFG_BOX
    UINT32 ownerBoxId;
    LOS_DL_LIST *freeList = NULL;
    LosSemCB *semBase = NULL;
#endif

#ifdef LOSCFG_BOX
    /* Box seam: this entry owns the lock. A failed resolve returns here
     * rather than via OUT - OUT would run the delete hook with a NULL CB,
     * which the baseline never does for an invalid handle. */
    SCHEDULER_LOCK(intSave);
    if (OsSemGetCB(semHandle, &semDeleted) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_SEM_INVALID;
    }
    /* kernel-owned resources must not be destroyed by a box (box heap lock
     * defense) - same lock hold, right after the resolve */
    if (OsSIdKernelResClaimCheck(semHandle) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_SEM_INVALID;
    }
#else
    if (GET_SEM_INDEX(semHandle) >= (UINT32)LOSCFG_BASE_IPC_SEM_LIMIT) {
        return LOS_ERRNO_SEM_INVALID;
    }

    semDeleted = GET_SEM(semHandle);

    SCHEDULER_LOCK(intSave);
#endif

    ret = OsSemStateVerify(semHandle, semDeleted);
    if (ret != LOS_OK) {
        goto OUT;
    }

    if (!LOS_ListEmpty(&semDeleted->semList)) {
        ret = LOS_ERRNO_SEM_PENDED;
        goto OUT;
    }

#ifdef LOSCFG_BOX
    /* refs LOS_SemDelete tail. The pool the CB returns to is chosen by the
     * ENTITY's owner, not by re-reading the sId's box field: the entity is the
     * authoritative record of which pool the CB was drawn from, and recycling
     * to the wrong one corrupts both pools. Must run BEFORE OsSIdObjDel, which
     * clears the entity. */
    ownerBoxId = OsEntityOwnerGetBySid(semHandle);
    if (ownerBoxId == (UINT32)LOS_KERNEL_SID_HEAD) {
        freeList = &g_unusedSemList;
        semBase = g_osAllSem;
    } else if (ownerBoxId < (UINT32)LOSCFG_BASE_BOX_MAX_LIMIT) {
        freeList = &g_boxFreeSemList[ownerBoxId];
        semBase = (LosSemCB *)g_boxCBArr[ownerBoxId].boxResSemList;
    } else {
        ret = LOS_ERRNO_SEM_INVALID;
        goto OUT;
    }
    (VOID)OsSIdObjDel(semHandle);
    OsSemNodeRecycle(semDeleted, freeList);

    OsSemDbgUpdateHook(semDeleted->semId, NULL, 0);

    /* Restore semId to the POOL INDEX (refs does the equivalent with
     * OS_BOX_GET_ID): OsSemCreateInternal reads it as the dataCBIdx for
     * OsSIdBind, so a CB left carrying a stale sId would bind at a bogus
     * index. Safe to do after the recycle - nothing reads semId until the
     * next create takes this CB off the free list. */
    semDeleted->semId = (UINT32)(semDeleted - semBase);
#else
    OsSemIdUpdate(semDeleted);
    OsSemNodeRecycle(semDeleted, &g_unusedSemList);

    OsSemDbgUpdateHook(semDeleted->semId, NULL, 0);
#endif

OUT:
    SCHEDULER_UNLOCK(intSave);

    LOS_TRACE(SEM_DELETE, semHandle, ret);
    OsHookCall(LOS_HOOK_TYPE_SEM_DELETE, semDeleted);
    return ret;
}

LITE_OS_SEC_TEXT UINT32 LOS_SemPend(UINT32 semHandle, UINT32 timeout)
{
    UINT32 intSave;
    LosSemCB *semPended = NULL;
    UINT32 ret;
    LosTaskCB *runTask = NULL;

#ifdef LOSCFG_BOX
    /* Box seam: the pre-lock checks keep their baseline position (unlocked),
     * then the sId resolves under this entry's own lock. */
    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_SEM_PEND_INTERR;
    }

    runTask = OsCurrTaskGet();
    if (runTask->taskFlags & OS_TASK_FLAG_SYSTEM) {
        PRINT_DEBUG("Warning: DO NOT recommend to use %s in system tasks.\n", __FUNCTION__);
        return LOS_ERRNO_SEM_PEND_IN_SYSTEM_TASK;
    }

    if (!OsPreemptable()) {
        return LOS_ERRNO_SEM_PEND_IN_LOCK;
    }

    SCHEDULER_LOCK(intSave);
    if (OsSemGetCB(semHandle, &semPended) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_SEM_INVALID;
    }

    LOS_TRACE(SEM_PEND, semHandle, semPended->semCount, timeout);
#else
    if (GET_SEM_INDEX(semHandle) >= (UINT32)LOSCFG_BASE_IPC_SEM_LIMIT) {
        return LOS_ERRNO_SEM_INVALID;
    }

    semPended = GET_SEM(semHandle);

    LOS_TRACE(SEM_PEND, semHandle, semPended->semCount, timeout);

    if (OS_INT_ACTIVE) {
        return LOS_ERRNO_SEM_PEND_INTERR;
    }

    runTask = OsCurrTaskGet();
    if (runTask->taskFlags & OS_TASK_FLAG_SYSTEM) {
        PRINT_DEBUG("Warning: DO NOT recommend to use %s in system tasks.\n", __FUNCTION__);
        return LOS_ERRNO_SEM_PEND_IN_SYSTEM_TASK;
    }

    if (!OsPreemptable()) {
        return LOS_ERRNO_SEM_PEND_IN_LOCK;
    }

    SCHEDULER_LOCK(intSave);
#endif

    ret = OsSemStateVerify(semHandle, semPended);
    if (ret != LOS_OK) {
        goto OUT;
    }
    OsSemDbgTimeUpdateHook(semHandle);
    if (semPended->semCount > 0) {
        semPended->semCount--;
        goto OUT;
    } else if (timeout == 0) {
        ret = LOS_ERRNO_SEM_UNAVAILABLE;
        goto OUT;
    }

    runTask->taskSem = (VOID *)semPended;
    OsHookCall(LOS_HOOK_TYPE_SEM_PEND, semPended, runTask, timeout);

    OsSchedWait(runTask, &semPended->semList, timeout);

    if (runTask->taskStatus & OS_TASK_STATUS_TIMEOUT) {
        runTask->taskStatus &= ~OS_TASK_STATUS_TIMEOUT;
        ret = LOS_ERRNO_SEM_TIMEOUT;
    }

OUT:
    SCHEDULER_UNLOCK(intSave);
    if (ret == LOS_OK) {
        OsHookCall(LOS_HOOK_TYPE_SEM_PEND, semPended, runTask, timeout);
    }
    return ret;
}

LITE_OS_SEC_TEXT UINT32 LOS_SemPost(UINT32 semHandle)
{
    UINT32 intSave;
    LosSemCB *semPosted = NULL;
    LosTaskCB *resumedTask = NULL;
    UINT16 maxCount;
    UINT32 ret;

#ifdef LOSCFG_BOX
    /* Box seam: this entry owns the lock; a failed resolve returns here
     * (OUT would run the post hook with a NULL CB). */
    SCHEDULER_LOCK(intSave);
    if (OsSemGetCB(semHandle, &semPosted) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_SEM_INVALID;
    }

    LOS_TRACE(SEM_POST, semHandle, semPosted->semType, semPosted->semCount);
#else
    if (GET_SEM_INDEX(semHandle) >= (UINT32)LOSCFG_BASE_IPC_SEM_LIMIT) {
        return LOS_ERRNO_SEM_INVALID;
    }

    semPosted = GET_SEM(semHandle);

    LOS_TRACE(SEM_POST, semHandle, semPosted->semType, semPosted->semCount);

    SCHEDULER_LOCK(intSave);
#endif

    ret = OsSemStateVerify(semHandle, semPosted);
    if (ret != LOS_OK) {
        goto OUT;
    }

    OsSemDbgTimeUpdateHook(semHandle);

    maxCount = (semPosted->semType == OS_SEM_COUNTING) ? LOS_SEM_COUNT_MAX : OS_SEM_BINARY_COUNT_MAX;
    if (semPosted->semCount >= maxCount) {
        ret = LOS_ERRNO_SEM_OVERFLOW;
        goto OUT;
    }

    if (LOS_ListEmpty(&semPosted->semList)) {
        semPosted->semCount++;
        goto OUT;
    }

    resumedTask = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&(semPosted->semList)));
    resumedTask->taskSem = NULL;
    OsSchedWake(resumedTask);

    SCHEDULER_UNLOCK(intSave);
    LOS_MpSchedule(OS_MP_CPU_ALL);
    LOS_Schedule();
    OsHookCall(LOS_HOOK_TYPE_SEM_POST, semPosted, resumedTask);
    return LOS_OK;

OUT:
    SCHEDULER_UNLOCK(intSave);
    if (ret == LOS_OK) {
        OsHookCall(LOS_HOOK_TYPE_SEM_POST, semPosted, resumedTask);
    }
    return ret;
}

LITE_OS_SEC_TEXT UINT32 LOS_SemGetValue(UINT32 semHandle, INT32 *currVal)
{
#ifdef LOSCFG_BOX
    LosSemCB *sem = NULL;
    UINT32 intSave;

    if (currVal == NULL) {
        return LOS_ERRNO_SEM_PTR_NULL;
    }
    /* currVal is written back to the caller: validate it against the
     * current context before the CB walk (same shape as the sId check in
     * OsSemCreateInternal, los_sem_box.c). */
    if (OsCheckValidDataAddr((UINTPTR)currVal, sizeof(INT32)) != LOS_OK) {
        return LOS_ERRNO_SEM_PTR_NULL;
    }

    /* Box seam: this entry owns the lock, and a failed sId resolve returns
     * here instead of through a shared OUT path. */
    SCHEDULER_LOCK(intSave);
    if (OsSemGetCB(semHandle, &sem) != LOS_OK) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_SEM_INVALID;
    }

    *currVal = sem->semCount;
    SCHEDULER_UNLOCK(intSave);
    return LOS_OK;
#else
    LosSemCB *sem = GET_SEM(semHandle);

    if (GET_SEM_INDEX(semHandle) >= (UINT32)LOSCFG_BASE_IPC_SEM_LIMIT) {
        return LOS_ERRNO_SEM_INVALID;
    }

    *currVal = sem->semCount;
    return 0;
#endif
}

#endif /* (LOSCFG_BASE_IPC_SEM == 1) */

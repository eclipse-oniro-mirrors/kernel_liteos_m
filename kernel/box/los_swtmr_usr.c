/*
 * los_swtmr_usr.c — per-box software-timer mechanisms.
 *
 * The kernel scanner never runs a box timer's callback itself: it hands the
 * item to the owning box's deferred-callback ring, and that box's own task
 * consumes the ring - so the callback runs unprivileged inside the box that
 * owns the timer.
 *
 * Guarded by the LOSCFG_BOX && LOSCFG_BASE_CORE_SWTMR conjunction (see
 * los_swtmr_usr_pri.h for why the conjunction is needed).
 */
#include "los_swtmr_usr_pri.h"
#include "los_box_pri.h"
#include "los_event_sid.h"
#include "los_interrupt.h"
#include "los_error.h"
#include "los_task_pri.h"
#include "los_sched_pri.h"
#include "los_memory.h"      /* LOS_MemAlloc: box-heap user stack */
#include "securec.h"

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)

SwtmrBoxCtx g_swtmrBoxCtx[LOSCFG_BASE_BOX_MAX_LIMIT];

/* Resolve the owner of a timer being created: the running box when its
 * swtmr context is initialized, otherwise the kernel pool. */
UINT32 OsSwtmrGetOwnerUsr(VOID)
{
    const LosBoxCB *boxCB = OsGetCurBoxCB();

    if (OS_INT_INACTIVE && (boxCB != NULL)) {
        if ((boxCB->boxId < LOSCFG_BASE_BOX_MAX_LIMIT) &&
            (g_swtmrBoxCtx[boxCB->boxId].cbArray != NULL)) {
            return (UINT32)boxCB->boxId;
        }
        return SWTMR_OWNER_INVALID;
    }
    return SWTMR_OWNER_DEFAULT;
}

/* Take one timer from the free list of the resolved owner. Called with
 * g_swtmrSpin held (SWTMR_LOCK in LOS_SwtmrCreate). */
LosSwtmrCB *OsSwtmrFreeListGetUsr(UINT32 *owner)
{
    UINT32 own = OsSwtmrGetOwnerUsr();
    LosSwtmrCB *swtmr = NULL;
    SortLinkList *sortList = NULL;
    LOS_DL_LIST *freeList = (own == SWTMR_OWNER_KERNEL) ? &g_swtmrFreeList :
                            &g_swtmrBoxCtx[own].freeList;

    *owner = own;
    if ((own == SWTMR_OWNER_INVALID) || LOS_ListEmpty(freeList)) {
        return NULL;
    }

    sortList = LOS_DL_LIST_ENTRY(freeList->pstNext, SortLinkList, sortLinkNode);
    swtmr = LOS_DL_LIST_ENTRY(sortList, LosSwtmrCB, sortList);
    LOS_ListDelete(LOS_DL_LIST_FIRST(freeList));
    swtmr->owner = (UINT16)own;
    return swtmr;
}

/* Recycle a timer to the free list of its owner. Called with g_swtmrSpin
 * held. Unknown owners fall back to the kernel pool. */
VOID OsSwtmrFreeListInsertUsr(LosSwtmrCB *swtmr)
{
    if ((swtmr->owner != SWTMR_OWNER_KERNEL) &&
        (swtmr->owner < LOSCFG_BASE_BOX_MAX_LIMIT)) {
        LOS_ListTailInsert(&g_swtmrBoxCtx[swtmr->owner].freeList,
                           &swtmr->sortList.sortLinkNode);
    } else {
        LOS_ListTailInsert(&g_swtmrFreeList, &swtmr->sortList.sortLinkNode);
    }
}

/* Defer a box-owned expired timer to the owning box's swtmr task as a value
 * copy, then wake the task. Returns LOS_OK when the timer was consumed
 * (including dropped on a full queue); LOS_NOK when the timer is not
 * box-owned and the caller must keep the baseline delivery path. */
UINT32 OsSwtmrScanDeferUsr(LosSwtmrCB *swtmr)
{
    SwtmrBoxCtx *ctx = NULL;
    SwtmrHandlerItem *item = NULL;

    if ((swtmr->owner == SWTMR_OWNER_KERNEL) ||
        (swtmr->owner >= LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return LOS_NOK;
    }

    ctx = &g_swtmrBoxCtx[swtmr->owner];
    if ((ctx->swtmrQueue.freeCnt == 0) || (ctx->swtmrQueue.buf == NULL)) {
        /* queue full or box not ready: drop, never leak into kernel task */
        return LOS_OK;
    }

    item = &((SwtmrHandlerItem *)(VOID *)ctx->swtmrQueue.buf)[ctx->swtmrQueue.tail];
    item->handler = swtmr->handler;
    item->arg = swtmr->arg;
    ctx->swtmrQueue.tail = (UINT16)((ctx->swtmrQueue.tail + 1) % ctx->swtmrQueue.len);
    ctx->swtmrQueue.freeCnt--;
    (VOID)LOS_TaskEventWrite(ctx->swtmrTaskId, LOS_SWTMR_EVENT_MASK);
    return LOS_OK;
}

/* Consume one deferred callback item for the running box. Called from the
 * box's swtmr task; the item buffer address is validated against the box
 * data region first. */
UINT32 OsSwtmrHandlerItemGet(SwtmrHandlerItem *item)
{
    UINT32 intSave;
    const LosBoxCB *boxCB = OsGetCurBoxCB();

    SwtmrBoxCtx *ctx = NULL;
    BoxSwtmrQueue *queue = NULL;
    UINT32 ret = LOS_NOK;

    if ((OS_INT_ACTIVE) || (boxCB == NULL) ||
        (boxCB->boxId >= LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return LOS_NOK;
    }

    if (OsCheckValidDataAddr((UINTPTR)item, sizeof(SwtmrHandlerItem)) != LOS_OK) {
        return LOS_NOK;
    }

    ctx = &g_swtmrBoxCtx[boxCB->boxId];
    queue = &ctx->swtmrQueue;
    if (queue->buf == NULL) {
        return LOS_NOK;
    }

    intSave = LOS_IntLock();
    if (queue->freeCnt != queue->len) {
        (VOID)memcpy_s(item, sizeof(SwtmrHandlerItem),
                       &((const SwtmrHandlerItem *)(const VOID *)queue->buf)[queue->head],
                       sizeof(SwtmrHandlerItem));
        queue->head = (UINT16)((queue->head + 1) % queue->len);
        queue->freeCnt++;
        ret = LOS_OK;
    }
    LOS_IntRestore(intSave);
    return ret;
}

/* Address checks for timer creation from a box: the handler must live in a
 * box-executable region and the returned id in a box-writable region. */
UINT32 OsSwtmrCreateCheckUsr(SWTMR_PROC_FUNC handler, UINT32 *swtmrId)
{
    if (OsGetCurBoxCB() == NULL) {
        return LOS_OK; /* kernel context: no address restriction */
    }
    if ((OsCheckValidTextAddr((UINTPTR)handler, sizeof(UINTPTR)) != LOS_OK) ||
        (OsCheckValidDataAddr((UINTPTR)swtmrId, sizeof(UINT32)) != LOS_OK)) {
        return LOS_NOK;
    }
    return LOS_OK;
}

/* Reset the kernel-side per-box swtmr contexts (free-list heads). Called
 * from the tail of OsSwtmrInit. */
UINT32 OsSwtmrUsrInit(VOID)
{
    UINT32 i;

    for (i = 0; i < LOSCFG_BASE_BOX_MAX_LIMIT; i++) {
        (VOID)memset_s(&g_swtmrBoxCtx[i], sizeof(SwtmrBoxCtx), 0, sizeof(SwtmrBoxCtx));
        LOS_ListInit(&g_swtmrBoxCtx[i].freeList);
        g_swtmrBoxCtx[i].swtmrTaskId = (UINT32)-1;
    }
    return LOS_OK;
}

/* Attach a box-provided swtmr pool and callback queue to a box. Called by
 * the box core when a box is created. */
UINT32 OsSwtmrUsrBoxInit(UINT16 boxId, LosSwtmrCB *swtmList, UINT32 listLen,
                         UINTPTR queueBuf, UINT32 queueLen)
{
    UINT32 i;
    SwtmrBoxCtx *ctx = NULL;

    if ((boxId >= LOSCFG_BASE_BOX_MAX_LIMIT) || (swtmList == NULL) || (listLen == 0) ||
        (queueBuf == (UINTPTR)0) || (queueLen == 0) || (queueLen > LOSCFG_BASE_CORE_SWTMR_LIMIT)) {
        return LOS_ERRNO_SWTMR_NO_MEMORY;
    }

    ctx = &g_swtmrBoxCtx[boxId];
    if (ctx->cbArray != NULL) {
        return LOS_ERRNO_SWTMR_STATUS_INVALID;
    }

    (VOID)memset_s((VOID *)swtmList, sizeof(LosSwtmrCB) * listLen, 0, sizeof(LosSwtmrCB) * listLen);
    ctx->cbArray = swtmList;
    ctx->cbArrayLen = listLen;
    ctx->swtmrQueue.buf = (UINTPTR *)(VOID *)queueBuf;
    ctx->swtmrQueue.len = (UINT16)queueLen;
    ctx->swtmrQueue.freeCnt = (UINT16)queueLen;
    LOS_ListInit(&ctx->freeList);
    for (i = 0; i < listLen; i++) {
        swtmList[i].timerId = i;
        swtmList[i].owner = boxId;
        LOS_ListTailInsert(&ctx->freeList, &swtmList[i].sortList.sortLinkNode);
    }
    return LOS_OK;
}

/* Resolve an OS_BOX_SET_ID-encoded swtmr id to the owning box's pool.
 * OsSwtmrIdVerify in los_swtmr.c only walks the kernel array, so ids at or
 * past LOSCFG_BASE_CORE_SWTMR_LIMIT are routed here. */
LosSwtmrCB *OsSwtmrFromIdUsr(UINT32 swtmrId)
{
    UINT16 boxId = (UINT16)(((swtmrId >> 16) & 0xFFFFU) - 1U);
    UINT32 idx = swtmrId & 0xFFFFU;
    const SwtmrBoxCtx *ctx = NULL;

    if (boxId >= LOSCFG_BASE_BOX_MAX_LIMIT) {
        return NULL;
    }
    ctx = &g_swtmrBoxCtx[boxId];
    if ((ctx->cbArray == NULL) || (idx >= ctx->cbArrayLen)) {
        return NULL;
    }
    if (ctx->cbArray[idx].owner != boxId) {
        return NULL;
    }
    return &ctx->cbArray[idx];
}

/* Create the per-box swtmr task. The task must be a REAL box task (box
 * pool TCB, pool kernel stack, boxCB/curBoxId binding, born-INUSER): a
 * plain LOS_TaskCreate routes to the KERNEL pool here (its boxCB stays
 * NULL and the item consumption below rejects the caller), so this goes
 * through OsUsrTaskCreateOnly with a user stack from the box heap. Called
 * via the BoxSwtmrInit veneer from the owning box's entry task. */
UINT32 OsSwtmrUsrTaskInit(UINT16 boxId, UINT32 stackSize)
{
    LosBoxCB *boxCB = NULL;
    TSK_INIT_PARAM_S swtmrTask = {0};
    VOID *usrStack = NULL;
    UINT32 swtmrTaskId;
    UINT32 ret;

    if ((boxId >= LOSCFG_BASE_BOX_MAX_LIMIT) ||
        (g_swtmrBoxCtx[boxId].cbArray == NULL)) {
        return LOS_ERRNO_SWTMR_TASK_CREATE_FAILED;
    }
    boxCB = &g_userBoxCBArr[boxId];

    /* user stack from the box heap: by then the entry task has brought
     * the heap up (BoxSwtmrInit runs after the LOS_MemInit in the entry) */
    usrStack = LOS_MemAlloc(boxCB->layout[0].boxMemPoolAddr, stackSize);
    if (usrStack == NULL) {
        return LOS_ERRNO_SWTMR_NO_MEMORY;
    }

    swtmrTask.pfnTaskEntry = (TSK_ENTRY_FUNC)BoxSwtmrTask;
    swtmrTask.uwStackSize = stackSize;
    swtmrTask.pcName = (CHAR *)g_usrSwtmrTaskName;
    swtmrTask.usTaskPrio = 0;

    ret = OsUsrTaskCreateOnly(&swtmrTaskId, &swtmrTask, usrStack, boxCB,
                              (BOOL)STACK_TYPE_DYNAMIC);
    if (ret == LOS_OK) {
        /* born-INUSER tasks are created in the suspended state - the
         * activate step (LOS_TaskCreateStatic does the same at its tail)
         * must run or the task is never dispatched. */
        ret = LOS_TaskResume(swtmrTaskId);
    }
    if (ret == LOS_OK) {
        g_swtmrBoxCtx[boxId].swtmrTaskId = swtmrTaskId;
        /* NO OS_TASK_FLAG_SYSTEM here, deliberately. The box swtmr task must
         * be allowed to block on its embedded event; the SYSTEM flag makes
         * the event read path reject it (READ_IN_SYSTEM_TASK) and the prio-0
         * task busy-loops, starving every lower-priority task. */
    }
    return ret;
}

/* Clear a box's swtmr resources: stop ticking timers, recycle them to the
 * box free list and reset the callback queue. Called from the exception
 * destroy path. */
VOID OsSwtmrDestroyUsr(VOID *boxCB)
{
    const LosBoxCB *box = (const LosBoxCB *)boxCB;
    SwtmrBoxCtx *ctx = NULL;
    UINT32 i;

    if ((box == NULL) || (box->boxId >= LOSCFG_BASE_BOX_MAX_LIMIT)) {
        return;
    }

    ctx = &g_swtmrBoxCtx[box->boxId];
    if (ctx->cbArray == NULL) {
        return;
    }

    for (i = 0; i < ctx->cbArrayLen; i++) {
        LosSwtmrCB *swtmr = &ctx->cbArray[i];
        if (swtmr->state == OS_SWTMR_STATUS_TICKING) {
            OsSchedDeleteSortLink(&g_percpu[0].swtmrSortLink, &swtmr->sortList);
            swtmr->state = OS_SWTMR_STATUS_CREATED;
        }
        if (swtmr->state != OS_SWTMR_STATUS_UNUSED) {
            OsSwtmrFreeListInsertUsr(swtmr);
            swtmr->state = OS_SWTMR_STATUS_UNUSED;
        }
    }
    (VOID)memset_s(&ctx->swtmrQueue, sizeof(BoxSwtmrQueue), 0, sizeof(BoxSwtmrQueue));
    /* Release the pool binding as well. OsSwtmrUsrBoxInit refuses to bind
     * over a live ctx->cbArray, so leaving it set fails the box env build on
     * a destroy -> restart cycle; and the memset above already cleared the
     * ring's buf/freeCnt, so a merely tolerated re-entry would leave the box
     * with a dead ring that silently drops every callback. The box's swtmr
     * task is deleted later in the same teardown (OsTaskDestroy runs after
     * OsSwtmrDestroy); until then it finds buf == NULL in
     * OsSwtmrHandlerItemGet and goes back to blocking on its event. */
    ctx->cbArray = NULL;
    ctx->cbArrayLen = 0;
    ctx->swtmrTaskId = (UINT32)-1;    /* same "no task" value OsSwtmrUsrInit sets */
}

#endif /* LOSCFG_BOX && LOSCFG_BASE_CORE_SWTMR */

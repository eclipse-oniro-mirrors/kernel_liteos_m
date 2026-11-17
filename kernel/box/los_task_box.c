/*
 * los_task_box.c - the box-specific half of the task module.
 *
 * Everything that differs between a kernel task and a box user task lives
 * here, so the generic task module only carries small gated hooks (TCB/stack
 * routing and the encoded-id macros in los_task_pri.h).
 *
 * Contents:
 *   - per-box TCB / kernel-stack pools + OsBoxTaskInit
 *   - OsBoxTaskIdCheckInvalid: encoded-id validity against the owning pool
 *   - free-list routing OsBoxTaskGet/FreeUsedTaskCB
 *   - kernel stack pool alloc/free (the pool node embeds the stack, so
 *     topOfStack == node->stack and the node is recovered by cast)
 *   - user-stack recycle queue on delete
 *   - the generic-path hook surface OsTaskBox*: los_task.c calls these
 *     instead of inlining BOX branches (CBInit default, create TCB
 *     routing/rewind, the OsTaskStackFree box part + free-list routing, the
 *     delete-on-run defer)
 *   - the user-task create family: entry/runtime param checks,
 *     OsUsrTaskCreateOnly, OsFirstBoxTaskCreate
 *
 * The LOSCFG_TASK_STACK_REUSE / DRVBOX / dynload task branches are all OFF on
 * this target, so they are not carried here.
 */
#include "los_typedef.h"
#include "los_task_pri.h"
#include "los_task.h"
#include "los_memory.h"
#include "los_box_pri.h"
#include "los_sec_pri.h"        /* LosTaskStack: pool node embedding the stack */
#include "los_err.h"
#include "los_printf.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Per-box pools. The pool memory itself lives in box resource sections
 * (.box.res: kernel RW / box RO) and is wired by boxes_cfg; only the
 * pointers/lists live in kernel bss. g_boxFreeTask is shared with
 * los_task.c's recycle path (OsTaskStackFree routes by id). */
LosTaskCB *g_boxTaskCBArray[LOSCFG_BASE_BOX_MAX_LIMIT];
LOS_DL_LIST g_boxFreeTask[LOSCFG_BASE_BOX_MAX_LIMIT];
STATIC LOS_DL_LIST g_boxFreeKerStackList[LOSCFG_BASE_BOX_MAX_LIMIT];

/* Preset the encoded task ids here, the same way the kernel pool presets
 * g_osTaskCBArray[index].taskId at init. */
UINT32 OsBoxTaskInit(UINT16 boxId, LosTaskCB *taskList, LosTaskStack *stackList, UINT32 taskNum)
{
    UINT32 idx;

    if (boxId >= LOSCFG_BASE_BOX_MAX_LIMIT) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }
    if ((taskList == NULL) || (stackList == NULL) || (taskNum == 0)) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }

    LOS_ListInit(&g_boxFreeTask[boxId]);
    LOS_ListInit(&g_boxFreeKerStackList[boxId]);
    g_boxTaskCBArray[boxId] = taskList;
    for (idx = 0; idx < taskNum; idx++) {
        taskList[idx].taskStatus = OS_TASK_STATUS_UNUSED;
        taskList[idx].taskId = OS_BOX_SET_ID(boxId, idx);
        LOS_ListTailInsert(&g_boxFreeTask[boxId], &taskList[idx].pendList);
        LOS_ListTailInsert(&g_boxFreeKerStackList[boxId], &stackList[idx].pendList);
    }
    return LOS_OK;
}

/* Kernel ids index the kernel pool; box ids decode against their per-box
 * pool bounds. */
BOOL OsBoxTaskIdCheckInvalid(UINT32 taskId)
{
    LosBoxCB *boxCB = NULL;
    UINT32 boxId;

    if (OS_TASK_ID_IS_KER(taskId)) {
        return (OS_TSK_GET_INDEX(taskId) >= g_taskMaxNum) ? TRUE : FALSE;
    }
    boxId = OS_BOX_GET_BID(taskId);
    if (boxId >= LOSCFG_BASE_BOX_MAX_LIMIT) {
        return TRUE;
    }
    if (g_boxTaskCBArray[boxId] == NULL) {
        return TRUE;
    }
    boxCB = &g_boxCBArr[boxId];
    if (boxCB->boxStatus == OS_BOX_UNINIT) {
        return TRUE;
    }
    return (OS_BOX_GET_ID(taskId) >= boxCB->boxResTaskNum) ? TRUE : FALSE;
}

/* Box TCB free-list routing. */
UINT32 OsBoxTaskGetFreeTaskCB(LosTaskCB **taskCB, const LosBoxCB *boxCB)
{
    LOS_DL_LIST *freeTask = &g_boxFreeTask[boxCB->boxId];

    if (LOS_ListEmpty(freeTask)) {
        return LOS_ERRNO_TSK_TCB_UNAVAILABLE;
    }
    *taskCB = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(freeTask));
    LOS_ListDelete(LOS_DL_LIST_FIRST(freeTask));
    return LOS_OK;
}

VOID OsBoxTaskFreeUsedTaskCB(LosTaskCB *taskCB, const LosBoxCB *boxCB)
{
    LOS_ListAdd(&g_boxFreeTask[boxCB->boxId], &taskCB->pendList);
}

/*
 * Kernel stacks: the pool node EMBEDS the 512B stack (los_sec_pri.h
 * LosTaskStack), so the "allocation" returns node->stack and the node is
 * recovered later as (LosTaskStack *)topOfStack - no extra TCB field needed.
 */
VOID *OsBoxKernelStackAlloc(const LosBoxCB *boxCB, UINT32 stackSize)
{
    LOS_DL_LIST *node = NULL;
    LosTaskStack *stack = NULL;

    (VOID)stackSize;
    if (LOS_ListEmpty(&g_boxFreeKerStackList[boxCB->boxId])) {
        return NULL;
    }
    node = LOS_DL_LIST_FIRST(&g_boxFreeKerStackList[boxCB->boxId]);
    LOS_ListDelete(node);
    stack = LOS_DL_LIST_ENTRY(node, LosTaskStack, pendList);
    return (VOID *)stack->stack;
}

VOID OsBoxKernelStackFree(LosTaskCB *taskCB)
{
    LosTaskStack *stack = (LosTaskStack *)(VOID *)taskCB->topOfStack;

    LOS_ListTailInsert(&g_boxFreeKerStackList[OS_BOX_GET_BID(taskCB->taskId)], &stack->pendList);
}

VOID OsBoxKernelStackFreeNode(VOID *stackBase, const LosBoxCB *boxCB)
{
    LosTaskStack *stack = (LosTaskStack *)stackBase;

    LOS_ListAdd(&g_boxFreeKerStackList[boxCB->boxId], &stack->pendList);
}

/* A deleted dynamic user stack is queued for the box to free from its own
 * pool (usr-side LOS_TaskResRecycle). Static entry-task stacks are never
 * recycled. */
VOID OsBoxUserStackRecycle(LosTaskCB *taskCB)
{
    LosBoxCB *boxCB = &g_boxCBArr[OS_BOX_GET_BID(taskCB->taskId)];
    UINTPTR *stackList = (UINTPTR *)boxCB->boxResRecycleStackList;

    if (stackList == NULL) {
        LOS_Panic("BoxID: 0x%x, boxResRecycleStackList is NULL\n", boxCB->boxId);
    }
    if (boxCB->boxResRecycleStackNum >= boxCB->boxResTaskNum) {
        LOS_Panic("BoxID: 0x%x, boxResRecycleStackNum is overflow:%u\n",
                  boxCB->boxId, boxCB->boxResRecycleStackNum);
    }
    stackList[boxCB->boxResRecycleStackNum] = taskCB->userTopOfStack;
    boxCB->boxResRecycleStackNum++;
}

/* ---- Gate/Exit pattern ----
 * los_task.c calls generic gates; the BOX logic stays here. Declarations and
 * the BOX-off STATIC INLINE baseline live in los_task_pri.h. */

/* OsTaskCBInit: kernel-task default for curBoxId; the box create family
 * overrides it right after. */
VOID OsTaskBoxCBInitDefault(LosTaskCB *taskCB)
{
    taskCB->curBoxId = (UINT8)OS_KERNEL_BOX;
}

/* Create gate: get TCB from correct pool (kernel or box).
 * Replaces OsTaskBoxCreateTCBGet - now handles both branches internally. */
UINT32 OsTaskCreateTCBGet(LosTaskCB **taskCB, const VOID *boxPtr)
{
    if (boxPtr != NULL) {
        return OsBoxTaskGetFreeTaskCB(taskCB, (const LosBoxCB *)boxPtr);
    }
    /* kernel task path (BOX-off baseline equivalent) */
    if (LOS_ListEmpty(&g_losFreeTask)) {
        return LOS_ERRNO_TSK_TCB_UNAVAILABLE;
    }
    *taskCB = OS_TCB_FROM_PENDLIST(LOS_DL_LIST_FIRST(&g_losFreeTask));
    LOS_ListDelete(LOS_DL_LIST_FIRST(&g_losFreeTask));
    return LOS_OK;
}

/* Create exit (failure rewind): return TCB to correct pool.
 * Replaces OsTaskBoxCreateTCBRewind - now handles both branches internally. */
VOID OsTaskCreateTCBRewind(LosTaskCB *taskCB, const VOID *boxPtr)
{
    if (boxPtr != NULL) {
        OsBoxTaskFreeUsedTaskCB(taskCB, (const LosBoxCB *)boxPtr);
        return;
    }
    /* kernel task path (BOX-off baseline equivalent) */
    LOS_ListAdd(&g_losFreeTask, &taskCB->pendList);
}

/* Delete gate step 1 - resource release: unlink boxTaskList, return the pool
 * kernel stack (recomputed from topOfStack), queue the dynamic user stack,
 * clear the box fields. MUST run while the TCB fields are still live —
 * i.e. BEFORE OsTaskStackFree's topOfStack clear and the security memset.
 * Kernel ids: no box resources (their stack is freed by the generic
 * dynamic-allocation path). */
VOID OsTaskResRelease(LosTaskCB *taskCB, UINT32 taskId)
{
    if (OS_TASK_ID_IS_KER(taskId)) {
        return;
    }
    LOS_ListDelete(&taskCB->boxTaskList);       /* off the box's taskCBList */
    OsBoxKernelStackFree(taskCB);
    if (taskCB->staticStack == 0) {
        OsBoxUserStackRecycle(taskCB);
    }
    taskCB->userTopOfStack = 0;
    taskCB->userStackSize = 0;
    taskCB->userStackPointer = NULL;
    taskCB->boxCB = NULL;
}

/* Delete gate step 2 - TCB recycle: pendList back to its owning free list,
 * routed by encoded id. MUST run AFTER the security memset in
 * OsTaskStackFree: an earlier link would have its pendList pointers wiped by
 * it, and the next alloc would LOS_ListDelete a {NULL,NULL} node - a write
 * to address 0. */
VOID OsTaskTCBRecycle(LosTaskCB *taskCB, UINT32 taskId)
{
    if (!OS_TASK_ID_IS_KER(taskId)) {
        LOS_ListAdd(&g_boxFreeTask[OS_BOX_GET_BID(taskId)], &taskCB->pendList);
    } else {
        LOS_ListAdd(&g_losFreeTask, &taskCB->pendList);
    }
}

/* OsTaskDelActionOnRun static arm: a running box task parks on the recycle
 * list - freeing its pool kernel stack here would race the still-executing
 * context, so every delete-on-run goes through the recycle list; kernel
 * static tasks keep the immediate free. */
BOOL OsTaskBoxDelRunDefer(LosTaskCB *taskCB)
{
    if (OS_TASK_ID_IS_KER(taskCB->taskId)) {
        return FALSE;
    }
    LOS_ListTailInsert(&g_taskRecycleList, &taskCB->pendList);
    return TRUE;
}

/* Param check for the box ENTRY task: the entry must live in the box text
 * window. It reads boxCB->layout[0] directly instead of going through the
 * OsCheckValid*Addr family in los_box.c, because that family resolves the
 * box from the running context - and the entry task is created before any
 * box task has ever run. */
STATIC UINT32 OsBoxEntryTaskCheck(const LosBoxCB *boxCB, const TSK_INIT_PARAM_S *initParam)
{
    const BoxMemLayout *layout = &boxCB->layout[0];
    UINTPTR entry = (UINTPTR)initParam->pfnTaskEntry;

    if ((initParam->pfnTaskEntry == NULL) || (initParam->pcName == NULL)) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    if ((entry < layout->boxTextStartAddr) || (entry >= layout->boxTextEndAddr)) {
        return LOS_ERRNO_TSK_ID_INVALID;
    }
    if (initParam->usTaskPrio > OS_TASK_PRIORITY_LOWEST) {
        return LOS_ERRNO_TSK_PRIOR_ERROR;
    }
    return LOS_OK;
}

/* Task-stack type discriminator. STACK_TYPE_STATIC / STACK_TYPE_DYNAMIC live
 * in los_box_pri.h rather than here because the box swtmr task bring-up in
 * los_swtmr_usr.c is an external OsUsrTaskCreateOnly consumer.
 * Entry tasks use box-static user memory (never recycled); runtime-created
 * tasks allocate the user stack from the box heap and it is queued for the
 * box to free on task deletion. */

/*
 * The user-task create core. The kernel part (TCB from the box pool, kernel
 * stack from the box stack pool) goes through the generic OsTaskCreateOnly
 * with a box pointer; the generic path consumes uwStackSize as the KERNEL
 * stack size, so it is swapped to the pool stack size here and restored
 * after. The user context is crafted by the arch side
 * (OsBoxTskUserStackInit: kernel-ctx PC patched to the OsUserTaskEntry
 * prelude, user frame left with PC=NULL for the prelude to fill).
 *
 * taskStackType selects whether the user stack is box-static (entry task)
 * or heap-allocated (runtime task) - the latter is recycled on delete via
 * OsBoxUserStackRecycle.
 *
 * Not STATIC: the box swtmr task bring-up in los_swtmr_usr.c is an external
 * consumer and needs the same full binding (box pool TCB + kernel stack pool
 * + boxCB/curBoxId/INUSER). */
UINT32 OsUsrTaskCreateOnly(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack,
                           LosBoxCB *boxCB, BOOL taskStackType)
{
    UINT32 ret;
    UINT32 intSave;
    UINT32 userStackSize = initParam->uwStackSize;
    VOID *kernelStack = NULL;
    LosTaskCB *taskCB = NULL;
    VOID *usrStackSP = NULL;

    kernelStack = OsBoxKernelStackAlloc(boxCB, LOSCFG_USR_THREAD_KERNEL_STACK_SIZE);
    if (kernelStack == NULL) {
        return LOS_ERRNO_TSK_NO_MEMORY;
    }

    initParam->uwStackSize = LOSCFG_USR_THREAD_KERNEL_STACK_SIZE;
    ret = OsTaskCreateOnly(taskId, initParam, kernelStack, TRUE, (const VOID *)boxCB);
    initParam->uwStackSize = userStackSize;
    if (ret != LOS_OK) {
        OsBoxKernelStackFreeNode(kernelStack, boxCB);
        return ret;
    }

    taskCB = OS_TCB_FROM_TID(*taskId);
    usrStackSP = OsBoxTskUserStackInit(taskCB, usrTopStack, userStackSize);

    SCHEDULER_LOCK(intSave);     /* the whole binding is one atomic step */
    taskCB->userStackPointer = usrStackSP;
    taskCB->userTopOfStack = (UINTPTR)usrTopStack;
    taskCB->userStackSize = userStackSize;
    taskCB->staticStack = (taskStackType == STACK_TYPE_STATIC) ? 1 : 0;
    taskCB->boxCB = (VOID *)boxCB;
    taskCB->curBoxId = (UINT8)boxCB->boxId;
    /* born-INUSER: the task starts "as if switched out in user mode" - the
     * first PendSV dispatch lands in the entry function unprivileged. The
     * kernel-prelude descent lives in box/los_box_uspace.S. */
    taskCB->taskStatus |= OS_TASK_STATUS_INUSER;
    LOS_ListInit(&taskCB->boxTaskList);
    LOS_ListAdd(&boxCB->taskCBList, &taskCB->boxTaskList);
    SCHEDULER_UNLOCK(intSave);
    return LOS_OK;
}

/* Creates the box entry task and resumes it. */
UINT32 OsFirstBoxTaskCreate(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack, LosBoxCB *boxCB)
{
    UINT32 ret;

    if (boxCB == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }

    ret = OsBoxEntryTaskCheck(boxCB, initParam);
    if (ret != LOS_OK) {
        return ret;
    }

    ret = OsUsrTaskCreateOnly(taskId, initParam, topStack, boxCB, STACK_TYPE_STATIC);
    if (ret != LOS_OK) {
        return ret;
    }
    BOX_SET_INIT_STATUS(boxCB->boxStatus, OS_BOX_INIT);
    return LOS_TaskResume(*taskId);
}

/* Create-param check for runtime user tasks: the entry must live in the box
 * text window, the user stack pointer must be aligned and fall inside the box
 * data window, and the priority must be in range.
 * checkStack gates the caller-pointer verification; both current callers
 * (OsTaskCreateOnlyDynamicDo / OsTaskCreateStaticDo) pass TRUE, since the
 * user stack always comes in from the caller. */
STATIC UINT32 OsUsrTaskCreateParamCheck(const TSK_INIT_PARAM_S *initParam, const VOID *usrTopStack,
                                        const LosBoxCB *boxCB, BOOL checkStack)
{
    if (initParam->pfnTaskEntry == NULL || initParam->pcName == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    if (initParam->usTaskPrio > OS_TASK_PRIORITY_LOWEST) {
        return LOS_ERRNO_TSK_PRIOR_ERROR;
    }
    if (initParam->uwStackSize < LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE) {
        return LOS_ERRNO_TSK_STKSZ_TOO_SMALL;
    }
    {
        const BoxMemLayout *layout = &boxCB->layout[0];
        UINTPTR entry = (UINTPTR)initParam->pfnTaskEntry;
        if ((entry < layout->boxTextStartAddr) || (entry >= layout->boxTextEndAddr)) {
            return LOS_ERRNO_TSK_ID_INVALID;
        }
    }
    if (checkStack && (usrTopStack != NULL)) {
        if (((UINTPTR)usrTopStack & (LOSCFG_STACK_POINT_ALIGN_SIZE - 1U)) != 0U) {
            return LOS_ERRNO_TSK_STKSZ_NOT_ALIGN;
        }
        /* the user stack must be in the box data window (heap or box bss) */
        const BoxMemLayout *layout = &boxCB->layout[0];
        UINTPTR top = (UINTPTR)usrTopStack;
        UINTPTR end = top + initParam->uwStackSize;
        if ((top < layout->boxDataStartAddr) || (end > layout->boxDataEndAddr)) {
            return LOS_ERRNO_TSK_ID_INVALID;
        }
    }
    return LOS_OK;
}

/* The user-context dynamic create core. The user stack was allocated by the
 * caller (usr-side LOS_TaskCreateOnly from g_boxLocalPool) and passed in. */
STATIC UINT32 OsTaskCreateOnlyDynamicDo(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack)
{
    UINT32 ret;
    LosBoxCB *boxCB = OsGetCurBoxCB();

    if (boxCB == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    ret = OsUsrTaskCreateParamCheck(initParam, usrTopStack, boxCB, TRUE);
    if (ret != LOS_OK) {
        return ret;
    }
    return OsUsrTaskCreateOnly(taskId, initParam, usrTopStack, boxCB, STACK_TYPE_DYNAMIC);
}

/* Static-stack creates (syscall TASK_BASE+1/+15). The kernel already has
 * LOS_TaskCreateStatic / LOS_TaskCreateOnlyStatic with the same names and
 * signatures (gated by LOSCFG_TASK_STACK_STATIC_ALLOCATION - the idle task
 * uses them), so there is no separate shim here: the native CreateOnlyStatic
 * entry carries the BOX seam in los_task.c and routes a box context into this
 * Do (box validation + box-pool creation + INUSER binding); kernel contexts
 * keep the native path. */
UINT32 OsTaskCreateStaticDo(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack)
{
    UINT32 ret;
    LosBoxCB *boxCB = OsGetCurBoxCB();

    if (boxCB == NULL) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    ret = OsUsrTaskCreateParamCheck(initParam, usrTopStack, boxCB, TRUE);
    if (ret != LOS_OK) {
        return ret;
    }
    return OsUsrTaskCreateOnly(taskId, initParam, usrTopStack, boxCB, STACK_TYPE_STATIC);
}

/* Kernel-context dynamic create: no user stack is allocated here, the caller
 * provides it. Used when kernel code spawns a box runtime task. */
UINT32 OsTaskKernelCreateDynamicDo(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack)
{
    return OsTaskCreateOnlyDynamicDo(taskId, initParam, topStack);
}

/* Syscall TASK_BASE+38. User context: copy initParam from the user, then run
 * the dynamic create core. Kernel code in the OS_IN_KER_STATE case calls
 * OsTaskKernelCreateDynamicDo directly, so this entry always takes the user
 * path (it is only reached via svc). */
UINT32 OsTaskCreateOnlyDynamic(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack)
{
    UINT32 ret;
    UINT32 newTaskId;
    TSK_INIT_PARAM_S localParam;

    if ((taskId == NULL) || (initParam == NULL) || (usrTopStack == NULL)) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    /* The initParam struct lives in the calling box's memory - same address
     * space, so the kernel can read it straight off the user pointer. */
    localParam = *initParam;
    ret = OsTaskCreateOnlyDynamicDo(&newTaskId, &localParam, usrTopStack);
    if (ret != LOS_OK) {
        return ret;
    }
    *taskId = newTaskId;
    return LOS_OK;
}

/* Syscall TASK_BASE+39. Pops one user-stack base address off the calling
 * box's recycle list; returns NULL when the list is empty. The usr-side
 * LOS_TaskStackRecycle loops this and frees each via LOS_MemFree. */
VOID *OsTaskGetRecycleStack(VOID)
{
    UINT32 intSave;
    UINT32 stackNum;
    VOID *ret = NULL;
    UINTPTR *stackList;
    LosBoxCB *boxCB = OsGetCurBoxCB();

    if (boxCB == NULL) {
        return NULL;
    }
    stackList = (UINTPTR *)boxCB->boxResRecycleStackList;
    if (stackList == NULL) {
        return NULL;
    }
    SCHEDULER_LOCK(intSave);
    stackNum = boxCB->boxResRecycleStackNum;
    if (stackNum > 0) {
        ret = (VOID *)(stackList[stackNum - 1U]);
        stackList[stackNum - 1U] = (UINTPTR)NULL;
        boxCB->boxResRecycleStackNum--;
    }
    SCHEDULER_UNLOCK(intSave);
    return ret;
}

/* Iterates the box's taskCBList and deletes every task; OsTaskDelete unlinks
 * each TCB from that list (via OsTaskStackFree -> OsTaskResRelease), so the
 * loop terminates naturally. Called from OsBoxDestroy with the box already
 * non-current (HelloworldTask context) - deleting the currently running task
 * would need it requeued first, which this path does not do. */
VOID OsTaskDestroy(LosBoxCB *boxCB)
{
    LosTaskCB *delTask;

    if (boxCB == NULL) {
        return;
    }
    while (!LOS_ListEmpty(&boxCB->taskCBList)) {
        delTask = LOS_DL_LIST_ENTRY(boxCB->taskCBList.pstNext, LosTaskCB, boxTaskList);
        (VOID)OsTaskDelete(delTask->taskId, 0);
    }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

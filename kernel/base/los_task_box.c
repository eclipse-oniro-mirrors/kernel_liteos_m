/*
 * los_task_box.c — BOX task-module mechanisms (M5B).
 *
 * refs keeps this family woven through kernel_s/base/los_task.c (~750 lines);
 * per the phase-2 principle (large divergence -> new file) they live here,
 * refs-shaped and refs-named, while the generic task module only carries
 * small gated hooks (TCB/stack routing, the encoded-id macros in
 * los_task_pri.h).
 *
 * Contents (refs los_task.c line refs):
 *   - per-box TCB / kernel-stack pools + OsBoxTaskInit (L38-152)
 *   - OsBoxTaskIdCheckInvalid: refs OsTaskIdCheck semantics (L1309 call sites)
 *   - free-list routing OsBoxTaskGet/FreeUsedTaskCB (L807-826)
 *   - kernel stack pool alloc/free (L77-118; the pool node embeds the stack,
 *     so topOfStack == node->stack and the node is recovered by cast)
 *   - user-stack recycle queue on delete (OsTaskDelAction box part, L684-705)
 *   - the generic-path hook surface OsTaskBox* (M5B-R): los_task.c calls
 *     these instead of inline BOX branches (CBInit default, create TCB
 *     routing/rewind, OsTaskStackFree box part + free-list routing, the
 *     delete-on-run defer)
 *   - the user-task create family: param-check subset (L555-647, the full
 *     check-valid family is M5D), OsUsrTaskCreateOnly (L945-983),
 *     OsFirstBoxTaskCreate (L1058-1087)
 * Trimmed of LOSCFG_TASK_STACK_REUSE / DRVBOX / dynload branches (all OFF).
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

/* Per-box pools (refs los_task.c:38-41). The pool memory itself lives in
 * box resource sections (.box.res, R2: kernel RW / box RO) and is wired by
 * boxes_cfg; only the pointers/lists live in kernel bss. g_boxFreeTask is
 * shared with los_task.c's recycle path (OsTaskStackFree routes by id). */
LosTaskCB *g_boxTaskCBArray[LOSCFG_BASE_BOX_MAX_LIMIT];
LOS_DL_LIST g_boxFreeTask[LOSCFG_BASE_BOX_MAX_LIMIT];
STATIC LOS_DL_LIST g_boxFreeKerStackList[LOSCFG_BASE_BOX_MAX_LIMIT];

/* refs OsBoxTaskInit (los_task.c:121-152): preset the encoded task ids —
 * OHOS also presets ids at pool init (g_osTaskCBArray[index].taskId), so the
 * shapes match. */
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

/* refs OsTaskIdCheck semantics: kernel ids index the kernel pool, box ids
 * decode against their per-box pool bounds. */
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

/* refs OsTaskGetFreeTaskCB / OsTaskFreeUsedTaskCB (los_task.c:807-826). */
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
 * recovered later as (LosTaskStack *)topOfStack — no extra TCB field needed.
 * refs los_task.c:77-91.
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

/* refs OsTaskDelAction box part (los_task.c:684-705): a deleted dynamic user
 * stack is queued for the box to free from its own pool (usr-side
 * LOS_TaskResRecycle, M5D). Static entry-task stacks are never recycled. */
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

/* ---- Gate/Exit pattern (M5B-R refactor, refs IPC_SEC design) ----
 * los_task.c calls generic gates; BOX logic stays here. Declarations +
 * BOX-off STATIC INLINE baseline live in los_task_pri.h. */

/* OsTaskCBInit: kernel-task default for curBoxId; the box create family
 * overrides it right after (refs OsUsrTaskCreateOnly does the same). */
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

/* Delete gate step 1 — resource release (refs OsTaskDelAction +
 * OsTaskStackFree box parts): unlink boxTaskList, return the pool kernel
 * stack (recomputed from topOfStack), queue the dynamic user stack,
 * clear the box fields. MUST run while the TCB fields are still live —
 * i.e. BEFORE OsTaskStackFree's topOfStack clear and the security memset.
 * Kernel ids: no box resources (their stack is freed by the generic
 * dynamic-allocation path). */
VOID OsTaskResRelease(LosTaskCB *taskCB, UINT32 taskId)
{
    if (OS_TASK_ID_IS_KER(taskId)) {
        return;
    }
    LOS_ListDelete(&taskCB->boxTaskList);       /* refs OsDelTaskCBFromTaskList */
    OsBoxKernelStackFree(taskCB);
    if (taskCB->staticStack == 0) {
        OsBoxUserStackRecycle(taskCB);
    }
    taskCB->userTopOfStack = 0;
    taskCB->userStackSize = 0;
    taskCB->userStackPointer = NULL;
    taskCB->boxCB = NULL;
}

/* Delete gate step 2 — TCB recycle: pendList back to its owning free list
 * (refs OsTaskStackFree freeTask routing by encoded id). MUST run AFTER
 * the security memset in OsTaskStackFree: an earlier link would have its
 * pendList pointers wiped by it, and the next alloc would LOS_ListDelete
 * a {NULL,NULL} node (write to address 0 — the pre-fix fault). */
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
 * context (refs routes every delete-on-run through the recycle list);
 * kernel static tasks keep the immediate free. */
BOOL OsTaskBoxDelRunDefer(LosTaskCB *taskCB)
{
    if (OS_TASK_ID_IS_KER(taskCB->taskId)) {
        return FALSE;
    }
    LOS_ListTailInsert(&g_taskRecycleList, &taskCB->pendList);
    return TRUE;
}

/* refs OsUsrTaskInitParamCheck subset for the box ENTRY task (los_task.c:
 * 555-647): entry must live in the box text window. The full check-valid
 * family (OsCheckValidTextWithBox etc.) lands with M5D. */
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

/* refs task-stack type discriminator (los_task.c STACK_TYPE_STATIC /
 * STACK_TYPE_DYNAMIC): entry tasks use box-static user memory (never
 * recycled); runtime-created tasks allocate the user stack from the box
 * heap and it is queued for the box to free on task deletion. */
#define STACK_TYPE_STATIC    0
#define STACK_TYPE_DYNAMIC   1

/*
 * refs OsUsrTaskCreateOnly (los_task.c:945-983) + OsUsrTaskKernelPartInit
 * (L915-931): the kernel part (TCB from the box pool, kernel stack from the
 * box stack pool) goes through the generic OsTaskCreateOnly with a box
 * pointer; the generic path consumes uwStackSize as the KERNEL stack size,
 * so it is swapped to the pool stack size here and restored after (refs
 * does the same swap). The user context is crafted by the arch side
 * (OsBoxTskUserStackInit: kernel-ctx PC patched to the OsUserTaskEntry
 * prelude, user frame left with PC=NULL for the prelude to fill).
 *
 * taskStackType selects whether the user stack is box-static (entry task)
 * or heap-allocated (runtime task) - the latter is recycled on delete via
 * OsBoxUserStackRecycle. */
STATIC UINT32 OsUsrTaskCreateOnly(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack,
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

    SCHEDULER_LOCK(intSave);     /* keeps the refs lock scope over the binding */
    taskCB->userStackPointer = usrStackSP;
    taskCB->userTopOfStack = (UINTPTR)usrTopStack;
    taskCB->userStackSize = userStackSize;
    taskCB->staticStack = (taskStackType == STACK_TYPE_STATIC) ? 1 : 0;
    taskCB->boxCB = (VOID *)boxCB;
    taskCB->curBoxId = (UINT8)boxCB->boxId;
    /* born-INUSER (pitfall 41): the task starts "as if switched out in user
     * mode" - the first PendSV dispatch lands in the entry function
     * unprivileged. The refs kernel-prelude descent is parked in
     * arch/los_box_uspace.S. */
    taskCB->taskStatus |= OS_TASK_STATUS_INUSER;
    LOS_ListInit(&taskCB->boxTaskList);
    LOS_ListAdd(&boxCB->taskCBList, &taskCB->boxTaskList);
    SCHEDULER_UNLOCK(intSave);
    return LOS_OK;
}

/* refs OsFirstBoxTaskCreate (los_task.c:1058-1087). */
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

/* refs OsUsrTaskCreateParamCheck subset (los_task.c:653-664): the entry must
 * live in the box text window, the user stack pointer must be aligned and
 * fall inside the box, and the priority must be in the user range. The full
 * OsCheckValid* family (M5D) hardens this; here we reuse the box text-window
 * check from OsBoxEntryTaskCheck and add the stack alignment + size floors.
 * checkStack is FALSE when the user stack was allocated by the kernel
 * (OsTaskInterCreate / STACK_REUSE) - no caller-provided pointer to verify. */
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

/* refs OsTaskCreateOnlyDynamicDo (los_task.c:1106-1114): the user-context
 * dynamic create core. The user stack was allocated by the caller (usr-side
 * LOS_TaskCreateOnly from g_boxLocalPool) and passed in. */
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

/* M5G: refs TASK_BASE+1/+15 static-stack creates. OHOS natives with the
 * SAME names and signatures already exist (los_task.c
 * LOS_TaskCreateStatic/OnlyStatic, gated LOSCFG_TASK_STACK_STATIC_
 * ALLOCATION — the idle task uses them), so unlike the mux face there is
 * no separate shim: the native CreateOnlyStatic entry carries the BOX
 * seam (los_task.c) and routes a box context into this Do (box
 * validation + box-pool creation + INUSER binding); kernel contexts keep
 * the native path. */
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

/* refs OsTaskKernelCreateDynamicDo (los_task.c:1188-1196): kernel-context
 * dynamic create (the kernel allocates no user stack here - the caller
 * provides it). Used when kernel code spawns a box runtime task. */
UINT32 OsTaskKernelCreateDynamicDo(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *topStack)
{
    return OsTaskCreateOnlyDynamicDo(taskId, initParam, topStack);
}

/* refs OsTaskCreateOnlyDynamic (los_task.c:1116-1139) - syscall TASK_BASE+38.
 * User context: copy initParam from the user, run the dynamic create core.
 * The kernel-context branch (refs OS_IN_KER_STATE) delegates to
 * OsTaskKernelCreateDynamicDo; here kernel code should call that directly,
 * so this entry always takes the user path (it is only reached via svc). */
UINT32 OsTaskCreateOnlyDynamic(UINT32 *taskId, TSK_INIT_PARAM_S *initParam, VOID *usrTopStack)
{
    UINT32 ret;
    UINT32 newTaskId;
    TSK_INIT_PARAM_S localParam;

    if ((taskId == NULL) || (initParam == NULL) || (usrTopStack == NULL)) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    /* OsTaskCopyParamFromUsr (refs los_task.c:629-646): the initParam struct
     * lives in the calling box's memory (same address space, kernel can
     * read it). The full OsCheckValidDataAddr hardening lands with M5D. */
    localParam = *initParam;
    ret = OsTaskCreateOnlyDynamicDo(&newTaskId, &localParam, usrTopStack);
    if (ret != LOS_OK) {
        return ret;
    }
    *taskId = newTaskId;
    return LOS_OK;
}

/* refs OsTaskGetRecycleStack (los_task.c:1367-1391, named UsTaskGetRecycleStack
 * in refs - the typo is preserved in the refs symbol; we use the clean name)
 * - syscall TASK_BASE+39. Pops one user-stack base address off the calling
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

/* refs los_task.c:1866 (OsTasksDestroy). Iterates the box's taskCBList and
 * deletes every task; OsTaskDelete removes each TCB from the list via
 * OsTaskBoxResFree, so the loop terminates naturally. Called from
 * OsBoxDestroy with the box already non-current (HelloworldTask context).
 * If the caller were inside the box, refs requeues the current task first;
 * we assert the non-current invariant and keep the path simple. */
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

/*
 * los_box_mini.c — minimal BOX kernel (M2+M3).
 *
 * Ported subset of refs/liteos/kernel_s/base/los_box.c:
 *   - OsBoxInit: wire the box array, build each box environment (data copy,
 *     bss zero, box heap init), create the entry task and launch it in
 *     USER MODE by crafting the initial user context (dual-stack model).
 *   - OsBoxEnvBuild: copy .BOX_x_DATA from flash, zero .BOX_x_BSS,
 *     LOS_MemInit the box heap (moved from boxXEntry, kernel side now).
 * Deferred to M5+: SID pools, resource lists, check-valid family,
 * monitor/destroy.
 */
#include "los_typedef.h"
#include "los_task_pri.h"
#include "los_arch_context.h"
#include "los_memory.h"
#include "securec.h"
#include "los_box_pri.h"
#include "los_offsets.h"   /* generated: OS_TASK_STATUS_INUSER etc. */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Read by the user-side veneers (MPU R2: kernel RW / user RO). */
UINT32 g_sysMode __attribute__((section(".USER_READONLY_DATA"))) = 0;
volatile UINT16 g_curBoxId = OS_KERNEL_BOX;
UINTPTR g_boxLocalPool[OS_BOX_MEM_LAYOUT_NUM] = {0};

LosBoxCB *g_boxCBArr = NULL;
SysMemLayout *g_sysMemLayout = NULL;

LosBoxCB *OsGetCurBoxCB(VOID)
{
    LosTaskCB *taskCB = OsCurrTaskGet();
    return (taskCB != NULL) ? (LosBoxCB *)taskCB->boxCB : NULL;
}

STATIC UINT32 OsBoxEnvBuild(LosBoxCB *boxCB)
{
    /* boxes_auto.cfg initializes the address pairs but leaves the derived
     * size fields at zero, so the sizes are computed from the addresses
     * and written back for later consumers. */
    BoxMemLayout *layout = &boxCB->layout[0];
    UINT32 dataSize = (UINT32)(layout->boxDataLoadEndAddr - layout->boxDataLoadStartAddr);
    UINT32 bssSize = (UINT32)(layout->boxBssEndAddr - layout->boxBssStartAddr);

    layout->boxDataLoadSize = dataSize;
    layout->boxBssSize = bssSize;
    layout->boxMemPoolSize = (UINT32)(layout->boxDataEndAddr - (UINTPTR)layout->boxMemPoolAddr);

    if (dataSize != 0) {
        if (memcpy_s((VOID *)layout->boxDataStartAddr, dataSize,
                     (const VOID *)layout->boxDataLoadStartAddr,
                     dataSize) != EOK) {
            return LOS_NOK;
        }
    }
    if (bssSize != 0) {
        if (memset_s((VOID *)layout->boxBssStartAddr, bssSize,
                     0, bssSize) != EOK) {
            return LOS_NOK;
        }
    }

    /* Box heap: kernel-side init (was boxXEntry's job in the old design,
     * impossible in user mode). */
    if (LOS_MemInit(layout->boxMemPoolAddr, layout->boxMemPoolSize) != LOS_OK) {
        return LOS_NOK;
    }
    return LOS_OK;
}

/* User task exit trampoline (COMMON_FLASH): a box entry function that
 * simply returns lands here and terminates via U_LOS_TaskUsrExit. */
extern VOID OsUsrTaskExit(VOID);

STATIC UINT32 OsBoxUsrStackInit(LosBoxCB *boxCB, LosTaskCB *taskCB)
{
    /* Craft the initial USER context: the task is created "as if it had
     * been switched out in user mode", so the very first dispatch lands
     * in boxEntryTask unprivileged with psp on the user stack. */
    TaskContext *userCtx = NULL;
    UINT8 *userStack = boxCB->stack;
    UINT32 userStackSize = boxCB->entryTaskStackSize;

    if ((userStack == NULL) || (userStackSize < sizeof(TaskContext) + sizeof(UINTPTR))) {
        return LOS_NOK;
    }

    userCtx = (TaskContext *)(userStack + userStackSize - sizeof(TaskContext));
    (VOID)memset_s(userCtx, sizeof(TaskContext), 0, sizeof(TaskContext));

    userCtx->uwPriMask = 0;
    userCtx->uwR0 = (UINT32)(UINTPTR)taskCB->taskId;
    /* LR = exit trampoline, thumb bit forced (refs task.c OsTskUserStackInit:
     * the task returns through a direct bx lr; an even target raises
     * INVSTATE). Without this a returning entry function would branch to 0. */
    userCtx->uwLR = (UINT32)(UINTPTR)OsUsrTaskExit | 1;
    userCtx->uwPC = (UINT32)(UINTPTR)boxCB->boxEntryTask;
    userCtx->uwxPSR = 0x01000000U;           /* thumb */

    taskCB->userStackPointer = (VOID *)userCtx;
    taskCB->taskStatus |= OS_TASK_STATUS_INUSER;
    return LOS_OK;
}

UINT32 OsBoxInit(LosBoxCB * const userBoxArr, SysMemLayout * const sysMemLayoutArr)
{
    UINT32 i;
    UINT32 ret;

    if ((userBoxArr == NULL) || (sysMemLayoutArr == NULL)) {
        return LOS_NOK;
    }

    g_boxCBArr = userBoxArr;
    g_sysMemLayout = sysMemLayoutArr;

    for (i = 0; i < LOSCFG_BASE_BOX_MAX_LIMIT; i++) {
        LosBoxCB *boxCB = &userBoxArr[i];
        TSK_INIT_PARAM_S param = {0};
        UINT32 taskID;
        LosTaskCB *taskCB = NULL;

        if (boxCB->boxEntryTask == NULL) {
            continue;
        }

        ret = OsBoxEnvBuild(boxCB);
        if (ret != LOS_OK) {
            return ret;
        }

        /* Kernel stack from the kernel heap (anchors the syscall graft);
         * its built-in context never runs - the task starts in user mode. */
        param.pfnTaskEntry = boxCB->boxEntryTask;
        param.usTaskPrio = boxCB->entryTaskPrio;
        param.uwStackSize = 0x400;
        param.pcName = boxCB->entryTaskName;

        ret = LOS_TaskCreate(&taskID, &param);
        if (ret != LOS_OK) {
            return ret;
        }

        taskCB = OS_TCB_FROM_TID(taskID);
        taskCB->boxCB = (VOID *)boxCB;

        ret = OsBoxUsrStackInit(boxCB, taskCB);
        if (ret != LOS_OK) {
            return ret;
        }

        BOX_SET_INIT_STATUS(boxCB->boxStatus, OS_BOX_INIT);
    }

    return LOS_OK;
}

#if (LOSCFG_BACKTRACE_TYPE == 1)
/*
 * Exception backtrace support: strong override of the weak default in
 * kernel/extended/backtrace/los_backtrace.c - the upstream extension point
 * for multi-code-section projects. Box-world return addresses live in
 * common text (veneers) and the box code slots besides kernel text - one
 * contiguous flash range on this board layout (asm/memmap_config.h).
 */
BOOL OsStackDataIsCodeAddr(UINTPTR value)
{
    if ((value > (UINTPTR)&__text_start) &&
        (value < (BOX_B_CODE_SLOT_BASE + BOX_B_CODE_SLOT_SIZE))) {
        return TRUE;
    }
    return FALSE;
}
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

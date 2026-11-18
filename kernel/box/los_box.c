/*
 * los_box.c — BOX kernel core: box lifecycle, environment build,
 * resource-pool wiring, validity checks, and destroy.
 *
 *   - OsBoxInit: boot-time stand-in for a per-box init syscall (one call
 *     per box from user space in the syscall design; the entry params live
 *     in the boxCB from the static config here). Per-box tail: status gate
 *     (OS_BOX_INITING) -> derived layout sizes -> OsBoxEnvBuild.
 *   - OsBoxEnvBuild: OsBoxDataBssInit (copy .BOX_x_DATA, zero .BOX_x_BSS)
 *     -> OsBoxResInit -> LOS_ListInit(taskCBList) -> create the box FIRST
 *     (entry) task through OsFirstBoxTaskCreate, which sets OS_BOX_INIT on
 *     success and returns the box to OS_BOX_UNINIT on failure. The task
 *     starts born-INUSER and enters user mode on first dispatch (PendSV
 *     INUSER path).
 *   - OsBoxResTaskInit: box-pool parameter/address validation for the task
 *     resource; the full check-valid family lives further down.
 *   - Current-context globals: g_sysMode / g_curBoxId / g_boxLocalPool all
 *     live in .USER_READONLY_DATA: the user world reads its box pool
 *     address directly, with no syscall, while the kernel writes them —
 *     dispatch on task switch.
 */
#include "los_typedef.h"
#include "los_task_pri.h"
#include "los_memory.h"
#include "los_memory_pri.h"   /* OsBoxMulPoolHeadClear decl (los_multipool.c) */
#include "securec.h"
#include "los_debug.h"
#include "los_box_pri.h"
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
/* LosSwtmrCB / the box swtmr section of the public header (resource-init
 * body below); usr_pri.h carries OsSwtmrUsrBoxInit. */
#include "los_swtmr.h"
#include "los_swtmr_usr_pri.h"
#endif
#if defined(LOSCFG_BOX) && (LOSCFG_BASE_IPC_EVENT == 1)
#include "los_event_sid.h"  /* LosEventCB / OsBoxEventInit / OsBoxEventListInit */
#endif
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_IPC_CONDVAR)
#include "los_condvar_pri.h"  /* LosCondVarCB / OsBoxCondInit / OsBoxCondListInit */
#endif
#include "los_sec_pri.h"        /* LosTaskStack */
#include "los_interrupt.h"      /* OS_INT_ACTIVE */
#include "los_exc.h"            /* OS_EXC_ACTIVE */
#ifdef LOSCFG_BASE_IPC_MUX
#include "los_mux_pri.h"        /* OsBoxMuxInit / OsBoxMuxListInit */
#endif
#ifdef LOSCFG_BASE_IPC_SEM
#include "los_sem_pri.h"        /* LosSemCB / OsBoxSemInit / OsBoxSemListInit */
#include "los_queue_pri.h"     /* LosQueueCB / OsBoxQueueInit / OsBoxQueueListInit */
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* g_sysMode / g_curBoxId / g_boxLocalPool (plus the ported-but-unused
 * g_curPerBoxData) are defined in los_usr_init.c - the refs usr-init file,
 * same R2 placement. This file keeps the target-only tick counter below. */
/* Shared tick counter for the user world (same shape as g_tickCount):
 * the kernel increments it in OsTickHandler (interrupt context, gated),
 * the box usr world reads it directly — no syscall round trip. The readers
 * are los_usr_swtmr_task.c (the handler running-ticks probe) and the boxA
 * tick test; a user-side LOS_TickCountGet wrapper is still deferred (see the
 * note in los_usr_api.c: the rename it needs collides with include order in
 * unrelated kernel TUs).
 * UINT32 keeps the aligned-word read atomic; the usr double-read tolerates
 * the (never shrinking) wraparound. */
LITE_OS_SEC_USER_RO_DATA volatile UINT32 g_usrTickCount = 0;

LosBoxCB *g_boxCBArr = NULL;
SysMemLayout *g_sysMemLayout = NULL;

/* Index the box array by the dispatch-maintained g_curBoxId; NULL for
 * kernel context (OS_KERNEL_BOX). */
LosBoxCB *OsGetCurBoxCB(VOID)
{
    if ((g_curBoxId & OS_KERNEL_BOX) == OS_KERNEL_BOX) {
        return NULL;
    }
    LOS_ASSERT(g_curBoxId < LOSCFG_BASE_BOX_MAX_LIMIT);
    return &g_boxCBArr[g_curBoxId];
}

STATIC UINT32 OsBoxResTaskInit(const LosBoxCB *box);

/* Copy the box data image from its flash load address and zero the bss.
 * The dynboxWord guard and the LASLR arm are omitted (no dynamic boxes /
 * no LASLR in this build); memcpy_s / memset_s stand in for plain
 * memcpy / memset (securec, -Werror). The derived sizes (boxDataLoadSize
 * / boxBssSize / boxMemPoolSize) are computed by the init path (OsBoxInit)
 * before this runs; the restart path reuses the stored values. */
STATIC UINT32 OsBoxDataBssInit(const LosBoxCB *box)
{
    const BoxMemLayout *ptr = NULL;
    UINT32 idx;

    for (idx = 0; idx < OS_BOX_MEM_LAYOUT_NUM; idx++) {
        ptr = &box->layout[idx];
        if ((ptr->boxDataLoadStartAddr != 0) && (ptr->boxDataLoadSize != 0)) {
            if (memcpy_s((VOID *)ptr->boxDataStartAddr,
                (UINT32)(((UINTPTR)ptr->boxDataEndAddr - (UINTPTR)ptr->boxDataStartAddr) -
                    (UINTPTR)ptr->boxBssSize - (UINTPTR)ptr->boxMemPoolSize),
                (const VOID *)ptr->boxDataLoadStartAddr, ptr->boxDataLoadSize) != EOK) {
                return LOS_ERRNO_BOX_MEMCPY_ERR;
            }
        }
        if ((ptr->boxBssSize != 0) &&
            (ptr->boxBssStartAddr >= ptr->boxDataStartAddr) &&
            ((UINTPTR)ptr->boxBssSize <= ptr->boxDataEndAddr) &&
            (ptr->boxBssStartAddr <= (ptr->boxDataEndAddr - ptr->boxBssSize))) {
            (VOID)memset_s((VOID *)ptr->boxBssStartAddr, ptr->boxBssSize, 0, ptr->boxBssSize);
        }
    }
    return LOS_OK;
}

/* Build the box environment: data/bss init -> resource pools ->
 * taskCBList -> create the box FIRST (entry) task through
 * OsFirstBoxTaskCreate, which sets OS_BOX_INIT on success; on create
 * failure the box returns to OS_BOX_UNINIT. The box heap is NOT touched
 * here: it is left to the entry task (user mode, LOS_MemInit in the box
 * entry), which re-runs on every restart. The monitor arm is omitted
 * (LOSCFG_SYSTEM_MONITOR off). */
STATIC UINT32 OsBoxEnvBuild(LosBoxCB *boxCB, UINT32 *taskId)
{
    TSK_INIT_PARAM_S tskInitParam = {0};
    UINT32 ret;
    VOID *stack = NULL;

    if (!BOX_IS_NOLOAD(boxCB->boxStatus)) {
        ret = OsBoxDataBssInit(boxCB);
        if (ret != LOS_OK) {
            return ret;
        }
    }
    ret = OsBoxResInit(boxCB);
    if (ret != LOS_OK) {
        return ret;
    }
    LOS_ListInit(&boxCB->taskCBList);
    tskInitParam.pfnTaskEntry = boxCB->boxEntryTask;
    tskInitParam.pcName = boxCB->entryTaskName;
    tskInitParam.uwStackSize = boxCB->entryTaskStackSize;
    tskInitParam.usTaskPrio = boxCB->entryTaskPrio;
    stack = (VOID *)boxCB->stack;
    ret = OsFirstBoxTaskCreate(taskId, &tskInitParam, stack, boxCB);
    if (ret != LOS_OK) {
        BOX_SET_INIT_STATUS(boxCB->boxStatus, OS_BOX_UNINIT);
        return LOS_ERRNO_BOX_TSK_INIT_ERR;
    }
    return LOS_OK;
}

STATIC UINT32 OsBoxResAddrCheck(UINTPTR address, UINTPTR size, UINTPTR start, UINTPTR end)
{
    if ((address < start) || (address > end) || (end <= size) || (address > (end - size))) {
        return LOS_NOK;
    }
    return LOS_OK;
}

/* Resource-parameter gate (non-TASK_STACK_REUSE form). */
STATIC_INLINE BOOL OsBoxResParmChk(const LosBoxCB *box)
{
    return ((UINTPTR)(box->boxResStackList) == 0) || ((UINTPTR)(box->boxResTaskList) == 0) ||
           (box->boxResTaskNum == 0);
}

/* The TCB / kernel-stack pools must live inside the box resource window
 * and are zeroed before use. */
STATIC UINT32 OsBoxResTaskInit(const LosBoxCB *box)
{
    if (OsBoxResParmChk(box)) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResTaskList), sizeof(LosTaskCB) * box->boxResTaskNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResStackList), sizeof(LosTaskStack) * box->boxResTaskNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    (VOID)memset(box->boxResStackList, 0, sizeof(LosTaskStack) * box->boxResTaskNum);
    (VOID)memset(box->boxResTaskList, 0, sizeof(LosTaskCB) * box->boxResTaskNum);
    return OsBoxTaskInit(box->boxId, (LosTaskCB *)(box->boxResTaskList),
        (LosTaskStack *)(box->boxResStackList), box->boxResTaskNum);
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

    /* Boot-time stand-in for a per-box init syscall (one call per box
     * from user space in the syscall design; the entry params live in the
     * boxCB from the static config here). Per-box tail: status gate
     * (OS_BOX_INITING) -> derived layout sizes -> OsBoxEnvBuild, whose
     * tail creates the box FIRST task and sets OS_BOX_INIT. */
    for (i = 0; i < LOSCFG_BASE_BOX_MAX_LIMIT; i++) {
        LosBoxCB *boxCB = &userBoxArr[i];
        UINT32 taskID;
        UINT32 idx;

        if (boxCB->boxEntryTask == NULL) {
            continue;
        }

        ret = OsGetBoxAndSetStatus(boxCB->boxId, &boxCB);
        if (ret != LOS_OK) {
            return ret;
        }

        /* Compute the derived layout sizes once; the restart path reuses
         * the stored values. */
        for (idx = 0; idx < OS_BOX_MEM_LAYOUT_NUM; idx++) {
            BoxMemLayout *ptr = &boxCB->layout[idx];
            ptr->boxDataLoadSize = (UINT32)(ptr->boxDataLoadEndAddr - ptr->boxDataLoadStartAddr);
            ptr->boxBssSize = (UINT32)(ptr->boxBssEndAddr - ptr->boxBssStartAddr);
            ptr->boxMemPoolSize =
                (UINT32)((UINTPTR)ptr->boxMemPoolEndAddr - (UINTPTR)ptr->boxMemPoolAddr);
        }

        ret = OsBoxEnvBuild(boxCB, &taskID);
        if (ret != LOS_OK) {
            return ret;
        }
    }

    return LOS_OK;
}

/* =====================================================================
 * check-valid + user-copy layer.
 * The WithBox/WithSection/Stack helpers run the address-window checks;
 * the trust-context short-circuit (IS_TRUST_CONTEXT is always TRUE while
 * IS_USER_ADDR() is stubbed) currently makes the top-level
 * OsCheckValid*Addr / OsCopy* accept any non-NULL pointer, until the real
 * userspace probe replaces the stub.
 * ===================================================================== */

STATIC_INLINE UINT32 OsCheckValidTextWithBox(UINTPTR address, UINT32 len, const LosBoxCB *boxCB)
{
    const BoxMemLayout *boxMem = NULL;
    const SysMemLayout *sysMem = NULL;
    UINT32 idx;

    for (idx = 0; idx < OS_BOX_MEM_LAYOUT_NUM; idx++) {
        boxMem = &boxCB->layout[idx];
        sysMem = &g_sysMemLayout[idx];
        if ((address >= boxMem->boxTextStartAddr) &&
            (len <= boxMem->boxTextEndAddr) &&
            (address <= (boxMem->boxTextEndAddr - len))) {
            return LOS_OK;
        }
        if ((address >= sysMem->sysUsrTextStartAddr) &&
            (len <= sysMem->sysUsrTextEndAddr) &&
            (address <= (sysMem->sysUsrTextEndAddr - len))) {
            return LOS_OK;
        }
    }
    return LOS_NOK;
}

STATIC_INLINE UINT32 OsCheckValidDataWithBox(UINTPTR address, UINT32 len, const LosBoxCB *boxCB)
{
    const BoxMemLayout *boxMem = NULL;
    const SysMemLayout *sysMem = NULL;
    UINT32 idx;

    for (idx = 0; idx < OS_BOX_MEM_LAYOUT_NUM; idx++) {
        boxMem = &boxCB->layout[idx];
        sysMem = &g_sysMemLayout[idx];
        if ((address >= boxMem->boxDataStartAddr) &&
            (len <= boxMem->boxDataEndAddr) &&
            (address <= (boxMem->boxDataEndAddr - len))) {
            return LOS_OK;
        }
        if ((address >= sysMem->sysUsrDataStartAddr) &&
            (len <= sysMem->sysUsrDataEndAddr) &&
            (address <= (sysMem->sysUsrDataEndAddr - len))) {
            return LOS_OK;
        }
    }
    return LOS_NOK;
}

STATIC_INLINE UINT32 OsCheckValidRodataWithBox(UINTPTR address, UINT32 len, const LosBoxCB *boxCB)
{
    const BoxMemLayout *boxMem = NULL;
    const SysMemLayout *sysMem = NULL;
    UINT32 idx;

    for (idx = 0; idx < OS_BOX_MEM_LAYOUT_NUM; idx++) {
        boxMem = &boxCB->layout[idx];
        sysMem = &g_sysMemLayout[idx];
        if ((address >= boxMem->boxRodataStartAddr) &&
            (len <= boxMem->boxRodataEndAddr) &&
            (address <= (boxMem->boxRodataEndAddr - len))) {
            return LOS_OK;
        }
        if ((address >= sysMem->sysUsrRodataStartAddr) &&
            (len <= sysMem->sysUsrRodataEndAddr) &&
            (address <= (sysMem->sysUsrRodataEndAddr - len))) {
            return LOS_OK;
        }
    }
    return LOS_NOK;
}

STATIC_INLINE UINT32 OsCheckValidStack(UINTPTR address, UINT32 len, const LosTaskCB *taskCB)
{
#ifdef LOSCFG_TASK_STACK_REUSE
    if ((address >= (UINTPTR)taskCB->userStackPointer) &&
        (len <= taskCB->stackSize) &&
        (address <= (taskCB->topOfStack + taskCB->stackSize - len))) {
        return LOS_OK;
    }
#else
    (VOID)address;
    (VOID)len;
    (VOID)taskCB;
#endif
    return LOS_NOK;
}

UINT32 OsCheckValidAddrWithSection(UINTPTR address, UINT32 len, const LosBoxCB *boxCB)
{
    if (boxCB == NULL) {
        return LOS_NOK;
    }
    if (OsCheckValidTextWithBox(address, len, boxCB) == LOS_OK) {
        return LOS_OK;
    }
    if (OsCheckValidDataWithBox(address, len, boxCB) == LOS_OK) {
        return LOS_OK;
    }
    return LOS_NOK;
}

UINT32 OsCheckValidAddrWithBox(UINTPTR address, UINT32 len)
{
    const LosTaskCB *taskCB = OsCurrTaskGet();
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    if (OsCheckValidAddrWithSection(address, len, boxCB) == LOS_OK) {
        return LOS_OK;
    }
    if (OsCheckValidStack(address, len, taskCB) == LOS_OK) {
        return LOS_OK;
    }
    return LOS_NOK;
}

UINT32 OsCheckValidTextAddr(UINTPTR address, UINT32 len)
{
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    if (OS_INT_ACTIVE || IS_TRUST_CONTEXT(boxCB) || OS_EXC_ACTIVE) {
        if (address == (UINTPTR)NULL) {
            return LOS_NOK;
        }
        return LOS_OK;
    }
    if (OsCheckValidTextWithBox(address, len, boxCB) == LOS_OK) {
        return LOS_OK;
    }
    return LOS_NOK;
}

UINT32 OsCheckValidRodataAddr(UINTPTR address, UINT32 len)
{
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    if (OS_INT_ACTIVE || IS_TRUST_CONTEXT(boxCB) || OS_EXC_ACTIVE) {
        if (address == (UINTPTR)NULL) {
            return LOS_NOK;
        }
        return LOS_OK;
    }
    if (OsCheckValidRodataWithBox(address, len, boxCB) == LOS_OK) {
        return LOS_OK;
    }
    return LOS_NOK;
}

UINT32 OsCheckValidPoolAddr(UINTPTR address, UINT32 len)
{
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    UINT32 idx;
    if (OS_INT_ACTIVE || IS_TRUST_CONTEXT(boxCB) || OS_EXC_ACTIVE) {
        if (address == (UINTPTR)NULL) {
            return LOS_NOK;
        }
        return LOS_OK;
    }
    for (idx = 0; idx < OS_BOX_MEM_LAYOUT_NUM; idx++) {
        if (address >= (UINTPTR)boxCB->layout[idx].boxMemPoolAddr &&
            (address + len) <= ((UINTPTR)boxCB->layout[idx].boxMemPoolAddr +
                                boxCB->layout[idx].boxMemPoolSize)) {
            break;
        }
    }
    if (idx == OS_BOX_MEM_LAYOUT_NUM) {
        return LOS_NOK;
    }
    return LOS_OK;
}

UINT32 OsCheckValidDataAddr(UINTPTR address, UINT32 len)
{
    const LosTaskCB *taskCB = OsCurrTaskGet();
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    if (OS_INT_ACTIVE || IS_TRUST_CONTEXT(boxCB) || OS_EXC_ACTIVE) {
        if (address == (UINTPTR)NULL) {
            return LOS_NOK;
        }
        return LOS_OK;
    }
    if (OsCheckValidDataWithBox(address, len, boxCB) == LOS_OK) {
        return LOS_OK;
    }
    if (OsCheckValidStack(address, len, taskCB) == LOS_OK) {
        return LOS_OK;
    }
    return LOS_NOK;
}

UINT32 OsCheckValidAddr(UINTPTR address, UINT32 len)
{
    const LosBoxCB *boxCB = OsGetCurBoxCB();
    if (OS_INT_ACTIVE || IS_TRUST_CONTEXT(boxCB) || OS_EXC_ACTIVE) {
        if (address == (UINTPTR)NULL) {
            return LOS_NOK;
        }
        return LOS_OK;
    }
    if (OsCheckValidAddrWithBox(address, len) == LOS_OK) {
        return LOS_OK;
    }
    return LOS_NOK;
}

UINT32 OsCopyFromUser(VOID *to, const VOID *from, UINT32 size)
{
    if ((to == NULL) || (size == 0)) {
        return LOS_NOK;
    }
    if (!LOS_IN_KERNEL) {
        return LOS_NOK;
    }
    if ((OsCheckValidDataAddr((UINTPTR)from, size) != LOS_OK) &&
        (OsCheckValidRodataAddr((UINTPTR)from, size) != LOS_OK)) {
        return LOS_NOK;
    }
    (VOID)memcpy(to, from, size);
    return LOS_OK;
}

UINT32 OsCopyToUser(VOID *to, const VOID *from, UINT32 size)
{
    if ((from == NULL) || (size == 0)) {
        return LOS_NOK;
    }
    if (!LOS_IN_KERNEL) {
        return LOS_NOK;
    }
    if (OsCheckValidDataAddr((UINTPTR)to, size) != LOS_OK) {
        return LOS_NOK;
    }
    (VOID)memcpy(to, from, size);
    return LOS_OK;
}

UINT32 OsStrlenFromUser(const CHAR *src, UINT32 count)
{
    CHAR character = '\0';
    UINT32 i = 0;
    UINT32 offset = 0;
    if ((src == NULL) || (count == 0)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if ((OsCheckValidDataAddr((UINTPTR)(src + offset), 1) != LOS_OK) &&
            (OsCheckValidRodataAddr((UINTPTR)(src + offset), 1) != LOS_OK)) {
            return 0;
        }
        character = *(CHAR *)(src + offset);
        if (character == '\0') {
            return offset;
        }
        ++offset;
    }
    return count + 1;
}

/* =====================================================================
 * per-box resource pool init family. Each wired arm validates the
 * box-provided pool against the box resource window, zeroes it, and hands
 * it to the module's per-box init (OsBoxXxxListInit / OsBoxXxxInit); the
 * remaining arms are compile-through stubs returning LOS_OK.
 * ===================================================================== */

#ifdef LOSCFG_BASE_IPC_EVENT
STATIC UINT32 OsBoxResEventInit(const LosBoxCB *box)
{
    /* Safe-IPC event pools, same shape as OsBoxResSemInit: free-list head ->
     * addr checks -> zero both arrays -> the module's per-box init, which
     * registers the entity array with the SID module and builds the free
     * list over the LosEventCB array. */
    UINT32 ret;

    if (OsBoxEventListInit(box->boxId) != LOS_OK) {
        return LOS_ERRNO_BOX_BOXID_ERR;
    }
    if (box->boxResEvtNum == 0) {
        return LOS_OK;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResEvtList),
                          sizeof(LosEventCB) * box->boxResEvtNum,
                          box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResEvtEntityList), sizeof(EntityObj) * box->boxResEvtNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    (VOID)memset(box->boxResEvtList, 0, sizeof(LosEventCB) * box->boxResEvtNum);
    (VOID)memset(box->boxResEvtEntityList, 0, sizeof(EntityObj) * box->boxResEvtNum);
    ret = OsBoxEventInit((EntityObj *)box->boxResEvtEntityList, (LosEventCB *)box->boxResEvtList,
                         box->boxResEvtNum, box->boxId);
    if (ret != LOS_OK) {
        return ret;
    }
    return LOS_OK;
}
#endif

#ifdef LOSCFG_BASE_IPC_CONDVAR
/* Condvar pools, same shape as OsBoxResSemInit / OsBoxResEventInit: free-list
 * head -> addr checks on both arrays -> zero them -> the module's per-box init
 * (which registers the entity array with the SID module and builds the free
 * list over the LosCondVarCB array). */
STATIC UINT32 OsBoxResCondInit(const LosBoxCB *box)
{
    UINT32 ret;

    if (OsBoxCondListInit(box->boxId) != LOS_OK) {
        return LOS_ERRNO_BOX_BOXID_ERR;
    }
    if (box->boxResCondNum == 0) {
        return LOS_OK;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResCondList),
                          sizeof(LosCondVarCB) * box->boxResCondNum,
                          box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResCondEntityList), sizeof(EntityObj) * box->boxResCondNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    (VOID)memset(box->boxResCondList, 0, sizeof(LosCondVarCB) * box->boxResCondNum);
    (VOID)memset(box->boxResCondEntityList, 0, sizeof(EntityObj) * box->boxResCondNum);
    ret = OsBoxCondInit((EntityObj *)box->boxResCondEntityList,
                        (LosCondVarCB *)box->boxResCondList,
                        box->boxResCondNum, box->boxId);
    if (ret != LOS_OK) {
        return ret;
    }
    return LOS_OK;
}
#endif

#ifdef LOSCFG_BASE_IPC_MUX
/* The LOSCFG_KERNEL_MUXDEP block is dropped: off in this build. */
STATIC UINT32 OsBoxResMuxInit(const LosBoxCB *box)
{
    UINT32 ret;
    if (OsBoxMuxListInit(box->boxId) != LOS_OK) {
        return LOS_ERRNO_BOX_BOXID_ERR;
    }
    if (box->boxResMuxNum == 0) {
        return LOS_OK;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResMuxList), sizeof(LosMuxCB) * box->boxResMuxNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResMuxEntityList), sizeof(EntityObj) * box->boxResMuxNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    (VOID)memset(box->boxResMuxList, 0, sizeof(LosMuxCB) * box->boxResMuxNum);
    (VOID)memset(box->boxResMuxEntityList, 0, sizeof(EntityObj) * box->boxResMuxNum);
    ret = OsBoxMuxInit((EntityObj *)box->boxResMuxEntityList, (LosMuxCB *)box->boxResMuxList,
                       box->boxResMuxNum, box->boxId);
    if (ret != LOS_OK) {
        return ret;
    }
    return LOS_OK;
}
#endif

#ifdef LOSCFG_BASE_IPC_QUEUE
STATIC UINT32 OsBoxResQueInit(const LosBoxCB *box)
{
    UINT32 ret;

    if (OsBoxQueueListInit(box->boxId) != LOS_OK) {
        return LOS_ERRNO_BOX_BOXID_ERR;
    }
    if (box->boxResQueNum == 0) {
        return LOS_OK;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResQueList), sizeof(LosQueueCB) * box->boxResQueNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResQueEntityList), sizeof(EntityObj) * box->boxResQueNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    (VOID)memset(box->boxResQueList, 0, sizeof(LosQueueCB) * box->boxResQueNum);
    (VOID)memset(box->boxResQueEntityList, 0, sizeof(EntityObj) * box->boxResQueNum);
    ret = OsBoxQueueInit((EntityObj *)box->boxResQueEntityList, (LosQueueCB *)box->boxResQueList,
                         box->boxResQueNum, box->boxId);
    if (ret != LOS_OK) {
        return ret;
    }
    return LOS_OK;
}
#endif

#ifdef LOSCFG_BASE_IPC_SEM
STATIC UINT32 OsBoxResSemInit(const LosBoxCB *box)
{
    UINT32 ret;

    if (OsBoxSemListInit(box->boxId) != LOS_OK) {
        return LOS_ERRNO_BOX_BOXID_ERR;
    }
    if (box->boxResSemNum == 0) {
        return LOS_OK;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResSemList), sizeof(LosSemCB) * box->boxResSemNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResSemEntityList), sizeof(EntityObj) * box->boxResSemNum,
        box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    (VOID)memset(box->boxResSemList, 0, sizeof(LosSemCB) * box->boxResSemNum);
    (VOID)memset(box->boxResSemEntityList, 0, sizeof(EntityObj) * box->boxResSemNum);
    ret = OsBoxSemInit((EntityObj *)box->boxResSemEntityList, (LosSemCB *)box->boxResSemList,
                       box->boxResSemNum, box->boxId);
    if (ret != LOS_OK) {
        return ret;
    }
    return LOS_OK;
}
#endif

#ifdef LOSCFG_BASE_IPC_SHMEM
STATIC UINT32 OsBoxResShmInit(const LosBoxCB *box)
{
    (VOID)box;
    return LOS_OK;
}
#endif

#ifdef LOSCFG_BASE_CORE_SWTMR
STATIC UINT32 OsBoxResSwtInit(const LosBoxCB *box)
{
    /* Swtmr list init -> addr checks -> swtmr pool init seam: attach the
     * box-provided timer pool + callback ring (the swtmr task itself is
     * created from the box entry task via BoxSwtmrInit). */
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
    UINT32 ret;

    if (box->boxResSwtNum == 0) {
        return LOS_OK;
    }
    if (OsBoxResAddrCheck((UINTPTR)(box->boxResSwtList),
                          sizeof(LosSwtmrCB) * box->boxResSwtNum,
                          box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    if (OsBoxResAddrCheck(box->boxResSwtQueue,
                          sizeof(UINTPTR) * box->boxResSwtQueueLen * 2U,
                          box->boxResStartAddr, box->boxResEndAddr) != LOS_OK) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    ret = OsSwtmrUsrBoxInit(box->boxId, (LosSwtmrCB *)box->boxResSwtList,
                            box->boxResSwtNum, box->boxResSwtQueue,
                            box->boxResSwtQueueLen);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
    (VOID)box;
    return LOS_OK;
}
#endif

#ifdef LOSCFG_COMPAT_POSIX_MQUEUE
STATIC UINT32 OsBoxResMQueueInit(const LosBoxCB *box)
{
    (VOID)box;
    return LOS_OK;
}
#endif

/* Resource-init dispatch. The module order mirrors the baseline box
 * resource-init sequence; keep it in sync when that baseline changes. */
UINT32 OsBoxResInit(LosBoxCB *box)
{
    UINT32 ret;

    if (box->boxResStartAddr >= box->boxResEndAddr) {
        return LOS_ERRNO_BOX_INVALID_ADDR;
    }
    ret = OsBoxResTaskInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
    ret = OS_SID_BOX_INIT(box);
    if (ret != LOS_OK) {
        return ret;
    }
#ifdef LOSCFG_BASE_IPC_EVENT
    ret = OsBoxResEventInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
#ifdef LOSCFG_BASE_IPC_CONDVAR
    ret = OsBoxResCondInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
#ifdef LOSCFG_BASE_IPC_MUX
    ret = OsBoxResMuxInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
#ifdef LOSCFG_BASE_IPC_QUEUE
    ret = OsBoxResQueInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
#ifdef LOSCFG_BASE_IPC_SEM
    ret = OsBoxResSemInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
#ifdef LOSCFG_BASE_IPC_SHMEM
    ret = OsBoxResShmInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
#ifdef LOSCFG_BASE_CORE_SWTMR
    ret = OsBoxResSwtInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
#ifdef LOSCFG_COMPAT_POSIX_MQUEUE
    ret = OsBoxResMQueueInit(box);
    if (ret != LOS_OK) {
        return ret;
    }
#endif
    return LOS_OK;
}

/* =====================================================================
 * box lifecycle.
 * OsGetBoxAndSetStatus gates on the box status under the scheduler lock.
 * OsBoxRestart: status gate -> OsBoxEnvBuild (data/bss, resource pools,
 * then the FIRST task is recreated inside; OsFirstBoxTaskCreate sets
 * OS_BOX_INIT). OsBoxDestroy runs the teardown sequence (SID release /
 * SID pool reset / swtmr / tasks).
 * OsBoxDestroy is compiled under LOSCFG_BOX directly rather than gated on
 * LOSCFG_EXC_ISOLATION (off in this build) to keep the lifecycle surface
 * exercisable.
 * ===================================================================== */

UINT32 OsGetBoxAndSetStatus(UINT16 boxId, LosBoxCB **boxCB)
{
    UINT32 idx;
    UINT32 intSave;

    SCHEDULER_LOCK(intSave);
    for (idx = 0; idx < KERNEL_BOX_LIMIT; idx++) {
        if (boxId == g_boxCBArr[idx].boxId) {
            if (!BOX_IS_UNINIT(g_boxCBArr[idx].boxStatus)) {
                SCHEDULER_UNLOCK(intSave);
                return LOS_ERRNO_BOX_STATUS_ERR;
            }
            *boxCB = &g_boxCBArr[idx];
            break;
        }
    }
    if (idx >= KERNEL_BOX_LIMIT) {
        SCHEDULER_UNLOCK(intSave);
        return LOS_ERRNO_BOX_BOXID_ERR;
    }
    BOX_SET_INIT_STATUS((*boxCB)->boxStatus, OS_BOX_INITING);
    SCHEDULER_UNLOCK(intSave);
    return LOS_OK;
}

/* Restart a box: status gate -> OsBoxEnvBuild, whose tail recreates the
 * box FIRST task and sets OS_BOX_INIT. The LOSCFG_DYNAMIC_BOX arm is
 * omitted (no dynamic boxes in this build). */
UINT32 OsBoxRestart(UINT16 boxId)
{
    UINT32 ret;
    UINT32 taskId;
    LosBoxCB *boxCB = NULL;

    ret = OsGetBoxAndSetStatus(boxId, &boxCB);
    if (ret != LOS_OK) {
        return ret;
    }
    return OsBoxEnvBuild(boxCB, &taskId);
}

VOID OsBoxDestroy(UINT16 boxId)
{
    LosBoxCB *boxCB = &g_boxCBArr[boxId];

    /* Teardown sequence: SIdDrvBoxIpcRelease -> SIdBoxResDestroy ->
     * SwtmrDestroy -> OsBoxMulPoolHeadClear (gated on
     * LOSCFG_MEM_MUL_POOL) -> OsTaskDestroy; Dynbox/Shell arms omitted
     * (not configured). The box heap itself is left to the restarted
     * entry task (LOS_MemInit in the box entry). */
    LOS_TaskLock();
    OsSIdDrvBoxIpcRelease(boxCB);
    OsSIdBoxResDestroy(boxCB);
    OsSwtmrDestroy(boxCB);
    /* Value check, not #ifdef: los_config.h #ifndef-defaults
     * LOSCFG_MEM_MUL_POOL to 0, so the macro is ALWAYS defined and #ifdef
     * would be permanently true. */
#if (LOSCFG_MEM_MUL_POOL == 1)
    OsBoxMulPoolHeadClear(boxId);
#endif
    OsTaskDestroy(boxCB);
    BOX_SET_INIT_STATUS(boxCB->boxStatus, OS_BOX_UNINIT);
    LOS_TaskUnlock();
}

/* ---- SID / swtmr destroy helpers ---- */

/* Releases IPC resources held by a driver box. No driver box in this
 * build, so the helper is a no-op kept for the destroy sequence. */
VOID OsSIdDrvBoxIpcRelease(const LosBoxCB *boxCB)
{
    (VOID)boxCB;
}

/* refs OsSIdBoxResDestroy, BASE_SID_LIST branch: there is no used-list to
 * walk, so the pool is scanned for live slots (sId != 0) and each one is
 * deleted - which unbinds its entity (owner / moduleData) and frees the slot.
 * refs' EXC_ISOLATION variant passes OsSIdRecycleByDestroy so each module's
 * destroyCb can hand the resource back; that whole block is compiled out here
 * (no OsSIdRegModuleHandle, so no destroyCb is ever registered) and the plain
 * delete path runs instead. That is sufficient because the box's CB arrays are
 * rebuilt from scratch by OsBoxResInit on the next start.
 * Ordering matters: OsBoxDestroy calls this BEFORE the status drops to
 * OS_BOX_UNINIT, so OsSIdObjGet's BOX_IS_INIT gate still admits the box. */
VOID OsSIdBoxResDestroy(const LosBoxCB *boxCB)
{
    SIdObj *sIdObj = NULL;
    UINT32 intSave;
    UINT32 i;

    if ((boxCB == NULL) || (boxCB->boxSIdPool == NULL) || (boxCB->boxSIdPoolSize == 0)) {
        return;
    }

    for (i = 0; i < boxCB->boxSIdPoolSize; i++) {
        sIdObj = &boxCB->boxSIdPool[i];
        if (sIdObj->sId == 0) {
            continue;
        }
        SCHEDULER_LOCK(intSave);
        (VOID)OsSIdObjDel(sIdObj->sId);
        SCHEDULER_UNLOCK(intSave);
    }
}

/* Destroys per-box swtmr resources. With the per-box swtmr machinery live
 * (boxA owns a timer pool), the box destroy sequence routes into the
 * swtmr usr destroy (stop ticking timers, recycle to the box free list,
 * reset the callback ring). */
VOID OsSwtmrDestroy(const LosBoxCB *boxCB)
{
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
    OsSwtmrDestroyUsr((VOID *)(UINTPTR)boxCB);
#else
    (VOID)boxCB;
#endif
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

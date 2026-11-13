/*
 * los_box.c — BOX kernel core (M5A: relocated to kernel/base, mirroring refs
 * kernel_s/base/los_box.c; M5B: env build gains the refs task-resource
 * pool init and the entry task goes through OsFirstBoxTaskCreate).
 *
 * Ported subset of refs/liteos/kernel_s/base/los_box.c:
 *   - OsBoxInit: boot-time stand-in for the refs LOS_BoxInit syscall (one
 *     call per box there, static config here). Per-box tail mirrors refs
 *     LOS_BoxInit: status gate (OS_BOX_INITING) -> derived layout sizes ->
 *     OsBoxEnvBuild.
 *   - OsBoxEnvBuild: refs los_box.c:738-767 — OsBoxDataBssInit (copy
 *     .BOX_x_DATA, zero .BOX_x_BSS) -> box heap init (port addition, see
 *     below) -> OsBoxResInit -> LOS_ListInit(taskCBList) -> create the box
 *     FIRST (entry) task through OsFirstBoxTaskCreate, which sets
 *     OS_BOX_INIT on success and returns the box to OS_BOX_UNINIT on
 *     failure. The task starts born-INUSER and enters user mode on first
 *     dispatch (PendSV INUSER path).
 *   - OsBoxResTaskInit: refs box-pool parameter/address validation family
 *     for the task resource (full check-valid family is M5D).
 *   - Current-context globals: g_sysMode / g_curBoxId / g_boxLocalPool all
 *     live in .USER_READONLY_DATA (refs los_usr_init.c semantics: the user
 *     world reads its box pool address directly, no syscall; the kernel
 *     writes them — dispatch on task switch).
 * Deferred to M5+: SID pools, IPC resource lists, check-valid family,
 * monitor/destroy.
 */
#include "los_typedef.h"
#include "los_task_pri.h"
#include "los_memory.h"
#include "los_memory_pri.h"   /* OsBoxMulPoolHeadClear decl (los_multipool.c) */
#include "securec.h"
#include "los_debug.h"
#include "los_box_pri.h"
#include "los_sec_pri.h"        /* LosTaskStack */
#include "los_interrupt.h"      /* OS_INT_ACTIVE */
#include "los_exc.h"            /* OS_EXC_ACTIVE */
#ifdef LOSCFG_BASE_IPC_MUX
#include "los_mux_pri.h"        /* OsBoxMuxInit / OsBoxMuxListInit (M5C) */
#endif
#ifdef LOSCFG_BASE_IPC_SEM
#include "los_sem_pri.h"        /* LosSemCB / OsBoxSemInit / OsBoxSemListInit (M5G) */
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* User-world readable globals (MPU R2: kernel RW / user RO). */
UINT32 g_sysMode __attribute__((section(".USER_READONLY_DATA"))) = 0;
volatile UINT32 g_curBoxId __attribute__((section(".USER_READONLY_DATA"))) = OS_KERNEL_BOX;
UINTPTR g_boxLocalPool[OS_BOX_MEM_LAYOUT_NUM] __attribute__((section(".USER_READONLY_DATA"))) = {0};
/* M5G: shared tick counter for the user world (refs g_tickCount shape):
 * the kernel increments it in OsTickHandler (interrupt context, gated),
 * the box usr world reads it directly via the double-read + DMB pattern
 * (usr_demo los_usr_api.c LOS_TickCountGet) — no syscall round trip.
 * UINT32 keeps the aligned-word read atomic; the usr double-read tolerates
 * the (never shrinking) wraparound. */
volatile UINT32 g_usrTickCount __attribute__((section(".USER_READONLY_DATA"))) = 0;

LosBoxCB *g_boxCBArr = NULL;
SysMemLayout *g_sysMemLayout = NULL;

/* refs los_box.c:886 — index the box array by the dispatch-maintained
 * g_curBoxId; OS_KERNEL_BOX for kernel context. */
LosBoxCB *OsGetCurBoxCB(VOID)
{
    if ((g_curBoxId & OS_KERNEL_BOX) == OS_KERNEL_BOX) {
        return NULL;
    }
    LOS_ASSERT(g_curBoxId < LOSCFG_BASE_BOX_MAX_LIMIT);
    return &g_boxCBArr[g_curBoxId];
}

STATIC UINT32 OsBoxResTaskInit(const LosBoxCB *box);

/* refs OsBoxDataBssInit (los_box.c:708-735): copy the box data image from
 * its flash load address and zero the bss. The refs dynboxWord guard and
 * the LASLR arm are omitted (no dynamic boxes / no LASLR in this build);
 * memcpy_s / memset_s stand in for the refs memcpy / memset (securec,
 * -Werror). The derived sizes (boxDataLoadSize / boxBssSize /
 * boxMemPoolSize) are computed by the init path (OsBoxInit) before this
 * runs, refs style; the restart path reuses the stored values. */
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

/* refs OsBoxEnvBuild (los_box.c:738-767): data/bss init -> resource pools
 * -> taskCBList -> create the box FIRST (entry) task through
 * OsFirstBoxTaskCreate, which sets OS_BOX_INIT on success; on create
 * failure the box returns to OS_BOX_UNINIT. The box heap is NOT touched
 * here: refs leaves it to the entry task (user mode, LOS_MemInit — refs
 * demo boxXEntry), which re-runs on every restart. The refs monitor arm
 * is omitted (LOSCFG_SYSTEM_MONITOR off). */
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

/* refs los_box.c:386-391 */
STATIC UINT32 OsBoxResAddrCheck(UINTPTR address, UINTPTR size, UINTPTR start, UINTPTR end)
{
    if ((address < start) || (address > end) || (end <= size) || (address > (end - size))) {
        return LOS_NOK;
    }
    return LOS_OK;
}

/* refs OsBoxResParmChk (non-TASK_STACK_REUSE form, los_box.c:397-406). */
STATIC_INLINE BOOL OsBoxResParmChk(const LosBoxCB *box)
{
    return ((UINTPTR)(box->boxResStackList) == 0) || ((UINTPTR)(box->boxResTaskList) == 0) ||
           (box->boxResTaskNum == 0);
}

/* refs OsBoxResTaskInit (los_box.c:408-426): the TCB / kernel-stack pools
 * must live inside the box resource window and are zeroed before use. */
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

    /* Boot-time stand-in for the refs LOS_BoxInit syscall (refs: one call
     * per box from user space; the entry params live in the boxCB from the
     * static config here). Per-box tail mirrors refs LOS_BoxInit: status
     * gate (OS_BOX_INITING) -> derived layout sizes -> OsBoxEnvBuild, whose
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

        /* refs LOS_BoxInit: compute the derived layout sizes once; the
         * restart path reuses the stored values. */
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
 * check-valid + user-copy layer (refs los_box.c:92-372).
 * Scaffold: the WithBox/WithSection/Stack helpers copy the refs address
 * window checks verbatim; the trust-context short-circuit (IS_TRUST_CONTEXT
 * is always TRUE while IS_USER_ADDR() is stubbed) makes the top-level
 * OsCheckValid*Addr / OsCopy* accept any non-NULL pointer. M5D wires the
 * real userspace probe.
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
 * per-box resource pool init family (refs los_box.c:408-706).
 * Scaffold: each arm is a compile-through stub returning LOS_OK; the real
 * OsBoxXxxListInit / OsBoxXxxInit bodies (los_mux.c / los_event.c / ...)
 * are not in the provided refs snapshot and are ported in M5C.
 * ===================================================================== */

#ifdef LOSCFG_BASE_IPC_EVENT
STATIC UINT32 OsBoxResEventInit(const LosBoxCB *box)
{
    (VOID)box;
    /* TODO(M5C): OsBoxEventListInit -> addr checks -> OsBoxEventInit. */
    return LOS_OK;
}
#endif

#ifdef LOSCFG_BASE_IPC_CONDVAR
STATIC UINT32 OsBoxResCondInit(const LosBoxCB *box)
{
    (VOID)box;
    return LOS_OK;
}
#endif

#ifdef LOSCFG_BASE_IPC_MUX
/* refs los_box.c:475-503 (LOSCFG_KERNEL_MUXDEP block dropped: off here). */
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
    (VOID)box;
    /* TODO(M5C): OsBoxQueueListInit -> addr checks -> OsBoxQueueInit. */
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
    (VOID)box;
    /* TODO(M5C): OsBoxSwtmrListInit -> addr checks -> OsBoxSwtmrInit. */
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

/* refs los_box.c:639-706 — dispatch order is verbatim. */
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
 * box lifecycle (refs los_box.c:769-884 + los_task.c:1866).
 * OsGetBoxAndSetStatus copies refs verbatim. OsBoxRestart is refs
 * verbatim: status gate -> OsBoxEnvBuild (data/bss, heap, resource pools,
 * then the FIRST task is recreated inside; OsFirstBoxTaskCreate sets
 * OS_BOX_INIT). OsBoxDestroy runs the refs teardown sequence (SID
 * release / SID pool reset / swtmr / tasks).
 * Deviation from refs: refs gates OsBoxDestroy on LOSCFG_EXC_ISOLATION;
 * this build has EXC_ISOLATION off, so the body is compiled under
 * LOSCFG_BOX directly to make the lifecycle surface exercisable.
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

/* refs OsBoxRestart (los_box.c:830-844): status gate -> OsBoxEnvBuild,
 * whose tail recreates the box FIRST task and sets OS_BOX_INIT. The refs
 * LOSCFG_DYNAMIC_BOX arm is omitted (no dynamic boxes in this build). */
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

    /* refs los_box.c:862-883 (gated LOSCFG_EXC_ISOLATION). Sequence is
     * verbatim: SIdDrvBoxIpcRelease -> SIdBoxResDestroy -> SwtmrDestroy ->
     * OsBoxMulPoolHeadClear (refs gates on LOSCFG_MEM_MUL_POOL) ->
     * OsTaskDestroy; Dynbox/Shell arms omitted (not configured). The box
     * heap itself is left to the restarted entry task (refs demo
     * LOS_MemInit). Deviation: refs compiles this under
     * LOSCFG_EXC_ISOLATION (off here); LOSCFG_BOX keeps the surface
     * exercisable. */
    LOS_TaskLock();
    OsSIdDrvBoxIpcRelease(boxCB);
    OsSIdBoxResDestroy(boxCB);
    OsSwtmrDestroy(boxCB);
    /* refs los_box.c:878 gates on #ifdef LOSCFG_MEM_MUL_POOL — refs (old
     * LiteOS) leaves the macro UNDEFINED when off. OHOS differs:
     * los_config.h #ifndef-defaults it to 0, so the macro is ALWAYS
     * defined and #ifdef would be permanently true; the value check
     * (los_memory_pri.h:281 form) carries the refs intent. */
#if (LOSCFG_MEM_MUL_POOL == 1)
    OsBoxMulPoolHeadClear(boxId);
#endif
    OsTaskDestroy(boxCB);
    BOX_SET_INIT_STATUS(boxCB->boxStatus, OS_BOX_UNINIT);
    LOS_TaskUnlock();
}

/* ---- SID / swtmr destroy helpers ---- */

/* refs: releases IPC resources held by a driver box. No driver box in
 * this build, so the helper is a no-op kept for the destroy sequence. */
VOID OsSIdDrvBoxIpcRelease(const LosBoxCB *boxCB)
{
    (VOID)boxCB;
}

/* Reset the box SID pool by re-running OsSIdBoxInit (memset + rebuild the
 * per-box free list). Idempotent with the init-time OS_SID_BOX_INIT. */
VOID OsSIdBoxResDestroy(const LosBoxCB *boxCB)
{
    if ((boxCB == NULL) || (boxCB->boxSIdPool == NULL) || (boxCB->boxSIdPoolSize == 0)) {
        return;
    }
    (VOID)OsSIdBoxInit(NULL, NULL, boxCB->boxSIdPool, boxCB->boxSIdPoolSize);
}

/* refs los_swtmr.c: destroys per-box swtmr resources. Both boxes have
 * swt limit == 0 in this build, so the helper is a no-op. */
VOID OsSwtmrDestroy(const LosBoxCB *boxCB)
{
    (VOID)boxCB;
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

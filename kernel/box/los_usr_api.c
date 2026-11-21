/*
 * los_usr_api.c — user-world API wrappers (single-copy, COMMON_TEXT).
 *
 * LOS_MuxCreate is defined here in the user world, wrapping the OsMuxCreate
 * syscall with the default attr (recursive + priority-inherit). There is ONE
 * copy, in .usr.text (COMMON_FLASH, MPU R0) — user-executable AND
 * kernel-callable:
 *   - box user code:   LOS_MuxCreate -> this wrapper -> OsMuxCreate veneer
 *                      -> svc -> SYS_OsMuxCreate
 *   - kernel modules without the rename root (posix pthread / pipe / cmsis,
 *                      prebuilt archives): plain-name call binds HERE, the
 *                      veneer's kernel branch tail-calls SYS_OsMuxCreate
 * Compiled in the kernel/box:box_usr module on purpose: that module gets the
 * usr include root and NOT the rename root, so the SYS_ rename never applies
 * inside this TU — the OsMuxCreate reference must stay PLAIN to reach the
 * veneer. (The sibling kernel/box module compiles under the rename because it
 * defines the syscall bodies.)
 */
#include "los_typedef.h"
#include "los_task.h"
#include "los_tick.h"       /* OS_SYS_MS_PER_SECOND + conversion prototypes */
#include "los_memory.h"    /* LOS_MemAllocAlign / LOS_MemFree */
#include "los_box_pri.h"   /* LITE_OS_SEC_TEXT_USRSPACE */
#include "syscalls.h"
#include <stdarg.h>

/* The box heap pool array lives in .USER_READONLY_DATA (R2, user-readable);
 * g_boxLocalPool[0] is the current box's heap pool (kernel-maintained). */
extern UINTPTR g_boxLocalPool[];

/* The user-readable tick counter (LITE_OS_SEC_USER_RO_DATA, R2 - written by
 * the kernel tick handler, plain-readable from user mode; same shape as the
 * swtmr task's own extern). */
extern volatile UINT32 g_usrTickCount;

/* attr values are advisory here: OsMuxCreate ignores attr because the mux
 * core is recursive + priority-inherit by construction. The names and values
 * are kept for API shape. */
#define OS_MUX_PROT_PRIO_INHERIT   0x1U
#define OS_MUX_TYPE_RECURSIVE      0x2U

/* The user-world wrappers carrying real logic live here: LOS_MuxCreate
 * (attr + OsMuxCreate syscall) and the task create/recycle family (stack
 * alloc from the box heap + the dynamic create / recycle syscalls). Memory
 * has NO wrapper: there are zero mem syscalls — the allocator lives in the
 * shared COMMON sections and the box entry task calls LOS_MemInit DIRECTLY
 * (LOSCFG_MEM_MUL_POOL is OFF, so no multipool registry walk exists).
 * The tick readout family is the refs USRSPACE direct-call face: pure
 * conversions and the user-readable tick counter - all plain-name copies
 * below, freed by the syscalls.h rename + local-prototype pair (the
 * include-order trap that once DEFERRED the TickCountGet wrapper). */

/* Refs conversion pair (kernel_s/base/los_tick.c, USRSPACE): ceil to the
 * next tick and the UINT32_MAX sentinel. NOTE the semantic split - the
 * OHOS vanilla pair floors and uses the OS_NULL_INT sentinel; the usr copy
 * stays faithful to refs, the same way the two worlds' LOS_TaskCreate
 * copies each stay faithful to their own source. */
LITE_OS_SEC_TEXT_USRSPACE __attribute__((noinline, used))
UINT32 LOS_MS2Tick(UINT32 millisec)
{
    UINT64 delayTick;

    if (millisec == UINT32_MAX) {
        return UINT32_MAX;
    }

    delayTick = (((UINT64)millisec * LOSCFG_BASE_CORE_TICK_PER_SECOND) +
                 OS_SYS_MS_PER_SECOND - 1U) / OS_SYS_MS_PER_SECOND;
    if (delayTick > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (UINT32)delayTick;
}

LITE_OS_SEC_TEXT_USRSPACE __attribute__((noinline, used))
UINT32 LOS_Tick2MS(UINT32 tick)
{
    UINT64 delayMs = ((UINT64)tick * OS_SYS_MS_PER_SECOND) / LOSCFG_BASE_CORE_TICK_PER_SECOND;
    if (delayMs > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (UINT32)delayMs;
}

/* Refs publishes the tick counter through the user-readable data section
 * (g_tickCount, RODATA_USRSPACE); this tree's counterpart is g_usrTickCount
 * (LITE_OS_SEC_USER_RO_DATA, written by the kernel tick handler). The
 * wrapper is a plain read - the usr path needs no syscall. */
LITE_OS_SEC_TEXT_USRSPACE __attribute__((noinline, used))
UINT64 LOS_TickCountGet(VOID)
{
    return (UINT64)g_usrTickCount;
}

LITE_OS_SEC_TEXT_USRSPACE __attribute__((noinline, used))
UINT32 LOS_MuxCreate(UINT32 *sId)
{
    return OsMuxCreate(sId, (UINT32)OS_MUX_PROT_PRIO_INHERIT | OS_MUX_TYPE_RECURSIVE);
}

/* LOS_TaskCreateOnly: allocate the user stack from the
 * box heap (g_boxLocalPool[0]) and create the task through the dynamic
 * create syscall. The task starts suspended; the caller resumes it (or
 * uses LOS_TaskCreate below). On failure the stack is freed. */
LITE_OS_SEC_TEXT_USRSPACE __attribute__((noinline, used))
UINT32 LOS_TaskCreateOnly(UINT32 *taskId, TSK_INIT_PARAM_S *initParam)
{
    VOID *stack;
    UINT32 stackSize;
    UINT32 ret;

    if ((taskId == NULL) || (initParam == NULL)) {
        return LOS_ERRNO_TSK_PTR_NULL;
    }
    stackSize = ALIGN(initParam->uwStackSize, LOSCFG_STACK_POINT_ALIGN_SIZE);
    stack = LOS_MemAllocAlign((VOID *)g_boxLocalPool[0], stackSize,
                              LOSCFG_STACK_POINT_ALIGN_SIZE);
    if (stack == NULL) {
        return LOS_ERRNO_TSK_NO_MEMORY;
    }
    ret = OsTaskCreateOnlyDynamic(taskId, initParam, stack);
    if (ret != LOS_OK) {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], stack);
        return ret;
    }
    return LOS_OK;
}

/* LOS_TaskCreate: create + resume (the task runs immediately). */
LITE_OS_SEC_TEXT_USRSPACE __attribute__((noinline, used))
UINT32 LOS_TaskCreate(UINT32 *taskId, TSK_INIT_PARAM_S *initParam)
{
    UINT32 ret = LOS_TaskCreateOnly(taskId, initParam);

    if (ret != LOS_OK) {
        return ret;
    }
    return LOS_TaskResume(*taskId);
}

/* LOS_TaskStackRecycle: drain the calling box's recycled user-stack
 * list and free each back to the box heap. The kernel pushes deleted
 * dynamic task stacks onto boxResRecycleStackList (OsBoxUserStackRecycle,
 * produced by OsTaskBoxResFree on delete); this consumer returns them to
 * the allocator. Call periodically (e.g. from the entry task loop). */
LITE_OS_SEC_TEXT_USRSPACE __attribute__((noinline, used))
VOID LOS_TaskStackRecycle(VOID)
{
    VOID *stack;

    while ((stack = OsTaskGetRecycleStack()) != NULL) {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], stack);
    }
}


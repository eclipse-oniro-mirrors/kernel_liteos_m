/*
 * los_usr_api.c — user-world API wrappers (single-copy, COMMON_TEXT).
 *
 * refs shape: kernel_s/usr/los_usr_api.c defines LOS_MuxCreate in the usr
 * world, wrapping the OsMuxCreate syscall with the default attr
 * (recursive + priority-inherit). refs compiles that file per box as part
 * of each box image; this one-world build keeps ONE copy in .usr.text
 * (COMMON_FLASH, MPU R0) — user-executable AND kernel-callable:
 *   - box user code:   LOS_MuxCreate -> this wrapper -> OsMuxCreate veneer
 *                      -> svc -> SYS_OsMuxCreate
 *   - kernel modules without the rename root (posix pthread / pipe / cmsis,
 *                      prebuilt archives): plain-name call binds HERE, the
 *                      veneer's kernel branch tail-calls SYS_OsMuxCreate
 * Compiled in the usr_demo module on purpose: its include root carries no
 * kernel/ subdir, so the SYS_ rename never applies inside this TU — the
 * OsMuxCreate reference must stay PLAIN to reach the veneer.
 */
#include "los_typedef.h"
#include "los_task.h"
#include "syscalls.h"
#include <stdarg.h>

/* The box heap pool array lives in .USER_READONLY_DATA (R2, user-readable);
 * g_boxLocalPool[0] is the current box's heap pool (kernel-maintained). */
extern UINTPTR g_boxLocalPool[];

/* attr values are advisory in this port (OsMuxCreate ignores attr — the
 * OHOS core is recursive + priority-inherit by construction); the refs
 * names/values shape is kept for API fidelity (refs los_mux.h). */
#define OS_MUX_PROT_PRIO_INHERIT   0x1U
#define OS_MUX_TYPE_RECURSIVE      0x2U

/* refs kernel_s/usr/los_usr_api.c keeps the user-world wrappers with real
 * logic here: LOS_MuxCreate (attr + OsMuxCreate syscall), the task
 * create/recycle family (stack alloc from the box heap + the dynamic
 * create / recycle syscalls). Memory has NO wrapper: M5D keeps zero mem
 * syscalls — the allocator lives in the shared COMMON sections and the
 * box entry task calls LOS_MemInit DIRECTLY (LOSCFG_MEM_MUL_POOL is OFF,
 * so the refs multipool registry walk does not exist).
 * M5G note: the R2-shared tick counter (g_usrTickCount) is published by
 * the kernel tick handler; its usr-side LOS_TickCountGet wrapper is
 * DEFERRED — the rename needed to free the plain symbol collides with
 * include-order in unrelated kernel TUs (los_event.c / debug family, the
 * prototype reaches them before the rename header) — the demo reads the
 * variable directly until the naming is solved. */

__attribute__((section(".usr.text"), noinline, used))
UINT32 LOS_MuxCreate(UINT32 *sId)
{
    return OsMuxCreate(sId, (UINT32)OS_MUX_PROT_PRIO_INHERIT | OS_MUX_TYPE_RECURSIVE);
}

/* refs los_usr_api.c LOS_TaskCreateOnly: allocate the user stack from the
 * box heap (g_boxLocalPool[0]) and create the task through the dynamic
 * create syscall. The task starts suspended; the caller resumes it (or
 * uses LOS_TaskCreate below). On failure the stack is freed. */
__attribute__((section(".usr.text"), noinline, used))
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

/* refs Los_TaskCreate: create + resume (the task runs immediately). */
__attribute__((section(".usr.text"), noinline, used))
UINT32 LOS_TaskCreate(UINT32 *taskId, TSK_INIT_PARAM_S *initParam)
{
    UINT32 ret = LOS_TaskCreateOnly(taskId, initParam);

    if (ret != LOS_OK) {
        return ret;
    }
    return LOS_TaskResume(*taskId);
}

/* refs LOS_TaskStackRecycle: drain the calling box's recycled user-stack
 * list and free each back to the box heap. The kernel pushes deleted
 * dynamic task stacks onto boxResRecycleStackList (OsBoxUserStackRecycle,
 * produced by OsTaskBoxResFree on delete); this consumer returns them to
 * the allocator. Call periodically (e.g. from the entry task loop). */
__attribute__((section(".usr.text"), noinline, used))
VOID LOS_TaskStackRecycle(VOID)
{
    VOID *stack;

    while ((stack = OsTaskGetRecycleStack()) != NULL) {
        (VOID)LOS_MemFree((VOID *)g_boxLocalPool[0], stack);
    }
}


/*
 * los_exc_usr.c — route user-box task faults to the box destroy flow.
 *
 * Shape notes:
 *  - the file guards on LOSCFG_BOX alone: the fault-to-destroy routing
 *    has no separate feature switch (kernel/box/BUILD.gn adds this file
 *    unconditionally, under the module-level switch);
 *  - the hook slot carries (UINT32 excType, EXC_CONTEXT_S *) and is
 *    invoked for every hardware fault (no EXC_TYPE filtering at the call
 *    site) - the phase/INUSER checks below do the routing;
 *  - destroy hands over to the box core (OsBoxDestroy on the running
 *    box), and the exception path continues after the hook returns
 *    (dump + ArchTaskExit halt) - the resources are torn down before the
 *    system stops;
 *  - every fault keeps the notify + dump path: registering this hook
 *    replaces the arch handler's own notify-and-display, so the hook must
 *    re-issue both (the arch file exposes the display helper through the
 *    BOX-only OsExcInfoDisplayBox wrapper) or faults become silent.
 */
#include "los_exc.h"
#include "los_debug.h"          /* OsDoExcHook / EXC_TYPE */
#include "los_task_pri.h"
#include "los_arch_interrupt.h" /* ExcInfo g_excInfo, ArchSetExcHook */
#include "los_box_pri.h"
#include "los_swtmr_pri.h"
#include "los_init.h"

/* arch/arm/cortex_m/los_interrupt.c: BOX-only wrapper around the STATIC
 * OsExcInfoDisplay, so the hook can keep the dump (see the call site). */
extern VOID OsExcInfoDisplayBox(const ExcInfo *excInfo);

#ifdef LOSCFG_BOX

/* True while the arch-level handler is unwinding a user box through the
 * destroy flow: the first fault does not clear nestCnt, so any nested
 * fault (nestCnt > 1) must not re-enter the destroy path. */
LITE_OS_SEC_TEXT BOOL ArchExcActiveUser(VOID)
{
    return (g_excInfo.nestCnt > 1) ? TRUE : FALSE;
}

/* Tear down the box-owned resources (swtmr) and hand over to the box
 * core destroy flow. */
LITE_OS_SEC_TEXT VOID OsExcDestroyUsr(VOID)
{
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
    OsSwtmrDestroyUsr((VOID *)OsGetCurBoxCB());
#endif
    /* "BOX_EXC" here is the diagnostic tag this message has always carried
     * (box-exception destroy), NOT the removed feature macro - the destroy
     * chain's acceptance records grep for this exact string. */
    PRINT_ERR("BOX_EXC: destroying box %d (fault in its task)\n", g_curBoxId);
    /* the box core owns the final teardown (the box destroy sequence) */
    OsBoxDestroy((UINT16)g_curBoxId);
}

/* Kernel exception hook: route user-box task faults to the box destroy
 * flow. Only in-task faults of an INUSER box task participate. */
STATIC VOID OsExcUsrHook(UINT32 excType, EXC_CONTEXT_S *excBufAddr)
{
    LosTaskCB *runTask = NULL;
    BOOL boxUserFault;

    (VOID)excType;
    (VOID)excBufAddr;

    if (ArchExcActiveUser()) {
        return;
    }

    runTask = OsCurrTaskGet();
    boxUserFault = ((g_excInfo.phase == OS_EXC_IN_TASK) && (runTask != NULL) &&
                    (runTask->boxCB != NULL) &&
                    ((runTask->taskStatus & OS_TASK_STATUS_INUSER) != 0)) ? TRUE : FALSE;

    /* Always keep the notify + dump: registering this hook replaced the arch
     * path's own display, and a fault that prints nothing ends in a silent
     * ArchTaskExit loop that can only be diagnosed with a debugger. Note a
     * fault raised inside a syscall has INUSER cleared, so it is dumped but
     * not attributed to the box for destruction. */
    OsDoExcHook(EXC_INTERRUPT);
    OsExcInfoDisplayBox(&g_excInfo);

    if (boxUserFault) {
        OsExcDestroyUsr();
    }
}

/* Single-slot hook registration (guaranteed available: the extended
 * exchook module, LOSCFG_PLATFORM_EXC, is off in this build). */
LITE_OS_SEC_TEXT_INIT UINT32 OsExcUsrInit(VOID)
{
    return ArchSetExcHook(OsExcUsrHook);
}
LOS_SYS_INIT(OsExcUsrInit, SYS_INIT_LEVEL_ARCH, SYS_INIT_SYNC_2);

#endif /* LOSCFG_BOX */

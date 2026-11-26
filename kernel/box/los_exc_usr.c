/* Diagnose box faults; the architecture decides whether exception return is safe. */
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

STATIC VOID OsExcUsrHook(UINT32 excType, EXC_CONTEXT_S *excBufAddr)
{
    (VOID)excType;
    (VOID)excBufAddr;
    if (!ArchExcActiveUser()) {
        OsDoExcHook(EXC_INTERRUPT);
        OsExcInfoDisplayBox(&g_excInfo);
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

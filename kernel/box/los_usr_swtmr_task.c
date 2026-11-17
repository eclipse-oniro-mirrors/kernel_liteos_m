/*
 * los_usr_swtmr_task.c — box swtmr task, user-domain side.
 *
 * Consumes the deferred callback items the kernel scanner produced
 * (los_swtmr_usr.c) and runs the handlers inside the owning box.
 *
 * Placement and reachability:
 *  - lives in kernel/box (the refs kernel_s/usr home, beside los_usr_init.c
 *    and los_usr_api.c) and compiles in the box_usr module, which gets the
 *    usr include root and NOT the rename root - so LOS_TaskEventRead below
 *    stays PLAIN and binds the COMMON_TEXT veneer;
 *  - the whole object is routed into COMMON_TEXT by the lds file-name rule
 *    (*los_usr_swtmr_task*.o), so no per-function section attributes are
 *    needed in this file;
 *  - LOS_TaskEventRead / OsSwtmrHandlerItemGet reach the kernel through
 *    the syscall veneers (numbers 133 / 139);
 *  - the running-ticks probe reads g_usrTickCount directly: the shared tick
 *    counter, published in a user-readable section.
 */
#include "los_typedef.h"
#include "los_swtmr_pri.h"
#include "los_event_sid.h"
#include "los_base.h"     /* LOS_WAIT_FOREVER */

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)

const CHAR g_usrSwtmrTaskName[] = "BOX_Swt_Task";

/* shared tick (kernel writes in OsTickHandler, box world reads directly) */
extern volatile UINT32 g_usrTickCount;

#define SWTMR_MAX_RUNNING_TICKS 10U

VOID BoxSwtmrTask(VOID)
{
    SwtmrHandlerItem item;
    UINT32 tick;

    for (;;) {
        (VOID)LOS_TaskEventRead(LOS_SWTMR_EVENT_MASK, LOS_WAIT_FOREVER);
        while (OsSwtmrHandlerItemGet(&item) == LOS_OK) {
            tick = g_usrTickCount;
            if (item.handler != NULL) {
                item.handler(item.arg);
            }
            tick = g_usrTickCount - tick;
            if (tick > SWTMR_MAX_RUNNING_TICKS) {
                PRINT_ERR("BoxSwtmrTask: handler 0x%x cost too many ticks %u\n",
                          (UINT32)(UINTPTR)item.handler, tick);
            }
        }
    }
}

/* Bring up the swtmr task of a box. Called from the box entry task (user
 * context of the owning box): the kernel-side implementation owns the task
 * creation and the ctx wiring. */
UINT32 BoxSwtmrInit(UINT16 boxId, UINT32 stackSize)
{
    return OsSwtmrUsrTaskInit(boxId, stackSize);
}

#endif /* LOSCFG_BOX && LOSCFG_BASE_CORE_SWTMR */

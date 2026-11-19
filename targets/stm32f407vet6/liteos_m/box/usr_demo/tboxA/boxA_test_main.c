/* boxA_test_main.c - UTS usr-phase box entry (test builds only).
 *
 * Under LOSCFG_KERNEL_TEST the boxes_auto.cfg entry override points boxA
 * at boxTestEntry (boxB stays down, NULL entry -> OsBoxInit skips it).
 * The box heap is already initialized by OsBoxEnvBuild (kernel side,
 * los_box_mini.c); this entry:
 *   1. parks on g_utsPhase (COMMON_RAM, RW both worlds) until the kernel
 *      coordinator (IT_TST_INI, osTest.c TestTaskEntry) finishes the
 *      kernel suites and sets GO;
 *   2. runs the usr test suites through the SHARED iCunit copy (COMMON
 *      sections) - same runner, same case table, same pass/fail counters
 *      as the kernel phase. ICunitInit is intentionally NOT called: the
 *      coordinator ran it and the kernel-phase results are live in the
 *      shared state;
 *   3. publishes DONE (the coordinator's bounded wait picks it up and
 *      prints the UNIFIED report) and parks forever as the box anchor.
 *
 * File name carries "boxA" on purpose: the boxes.ld selectors route every
 * *boxA*.o object into the boxA flash/RAM slots (usr-executable). */

#include "boxes_conf.h"
#include "los_task.h"
#include "syscalls.h"
#include "uts_phase.h"
#include "osTest.h"
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
#include "los_swtmr_usr_pri.h"   /* BoxSwtmrInit */
#endif

extern VOID ItSuiteLosBoxUsr(VOID);

VOID boxTestEntry(VOID)
{
    /* Re-initialize the box heap from USER context.
     * The pool lock mux has to be created HERE, not kernel-side: under BOX the
     * allocator is compiled once in the usr world, so MemLockCreate's
     * LOS_MuxCreate is the usr wrapper -> OsMuxCreate, which draws the CB from
     * boxA's own mux pool and the sId from boxA's SID pool. A lock created
     * kernel-side instead would carry a kernel-head sId over a kernel-table CB
     * that boxA's later LOS_MuxPend would have to resolve cross-domain.
     * OsBoxEnvBuild therefore leaves the box heap alone entirely. Skipping this
     * re-init was the PC=0x414 crash. */
    UINT32 memRet = LOS_MemInit((VOID *)OS_BOXA_POOL_ADDR, (UINT32)BOXA_HEAP_SIZE);
    if (memRet != LOS_OK) {
        LOS_PrintStr("UTS: boxA heap LOS_MemInit failed!\n");
        return;
    }

    /* Wait for the kernel phase: poll with the usr delay syscall (veneer
     * -> SVC). Delay granularity is ONE tick (this board's tick measures
     * ~1s, see the calibrate line - a larger value would stall the usr
     * phase for that long after GO). Unbounded on purpose - if the
     * coordinator never sets GO (OsBoxInit failure path), the box parks
     * here out of the way and the kernel run completes with its results. */
    while (g_utsPhase != UTS_PHASE_GO) {
        LOS_TaskDelay(1);
    }

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
    /* Bring up this box's swtmr task (the deferred-callback consumer).
     * Done AFTER the park on purpose: during the kernel
     * phase the prio-0 consumer must not be scheduled at all, so the usr
     * suites that need it (ItLosBoxUsr005) get it only for their own window.
     * Without this the box swtmr handlers never fire - every "wait for the
     * handler" phase times out. */
    {
        UINT32 swtRet = BoxSwtmrInit(LOS_BOXA_ID, 1024);
        if (swtRet != LOS_OK) {
            LOS_PrintStr("UTS: BoxSwtmrInit failed!\n");
        }
    }
#endif

    PRINTK("\t\n --- UTS usr phase (boxA, user mode) --- \n\n");
    ItSuiteLosBoxUsr();
    PRINTK("--- UTS usr phase done ---\n");

    g_utsPhase = UTS_PHASE_DONE;

    /* Park: the entry task remains as the box anchor. */
    while (1) {
        LOS_TaskDelay(0xFFFF);
    }
}

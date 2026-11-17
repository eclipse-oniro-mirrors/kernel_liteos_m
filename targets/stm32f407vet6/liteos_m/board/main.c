/*
 * STM32F407VET6 LiteOS-M minimal demo:
 * one task printing "helloworld" endlessly over USART1 (115200).
 */

#include <stdio.h>
#include "los_config.h"
#include "los_task.h"
#include "uart.h"
#ifdef LOSCFG_BASE_CORE_SWTMR
#include "los_swtmr.h"
#endif
/* Kernel-context baseline suite (KernelBaselineTest below). Each header is
 * pulled under its own LOSCFG gate so the suite tracks the IPC config. */
#if (LOSCFG_BASE_IPC_MUX == 1)
#include "los_mux.h"
#endif
#if (LOSCFG_BASE_IPC_SEM == 1)
#include "los_sem.h"
#endif
#if (LOSCFG_BASE_IPC_QUEUE == 1)
#include "los_queue.h"
#endif
#include "los_memory.h"
#ifdef LOSCFG_BOX
#include "los_box_pri.h"
#include "boxes_conf.h"
#if (LOSCFG_BASE_IPC_EVENT == 1)
#include "los_event_sid.h"   /* LOS_EventCreate / LOS_EventWriteWithSId */
#endif
#endif

#ifdef LOSCFG_KERNEL_TEST
/* Test build: LosAppInit is provided by testsuites/src/osTest.c (creates the
 * IT_TST_INI task that runs the ICunit suites). The HelloworldTask demo -
 * including the OsBoxInit call - is compiled out; the box suites assert the
 * kernel-context bookkeeping that holds with or without boxes initialized. */
extern UINT32 LosAppInit(VOID);
#else /* !LOSCFG_KERNEL_TEST */
static UINT32 g_helloworldTaskId;

#ifdef LOSCFG_BOX
#if (LOSCFG_BASE_IPC_EVENT == 1)
/* Cross-domain SID event. The kernel creates one - which draws from the
 * KERNEL event table (g_osAllEvent) and therefore yields a kernel-head sId -
 * writes a bit into it, and publishes the handle in R2 (kernel-writable,
 * user-readable, the same section g_usrTickCount uses). boxA then reads and
 * writes that same event through the id family, which is the only way to
 * exercise the kernel-head arm of the two-bound EVT resolve from user mode.
 *
 * This is exactly what the kernel table exists for: the pointer-addressed
 * event family (LOS_EventInit over a caller-owned EVENT_CB_S) cannot cross
 * into a box at all, since a box must not hold kernel pointers. */
LITE_OS_SEC_USER_RO_DATA volatile UINT32 g_kSharedEvtSId = 0;

static VOID KernelSharedEventSetup(VOID)
{
    UINT32 sId = 0;

    if (LOS_EventCreate(&sId) != LOS_OK) {
        printf("kernel shared event: create FAILED\n");
        return;
    }
    if (LOS_EventWriteWithSId(sId, 0x1U) != LOS_OK) {
        printf("kernel shared event: write FAILED\n");
        return;
    }
    /* publish last: boxA treats a non-zero handle as "ready to read 0x1" */
    g_kSharedEvtSId = sId;
    printf("kernel shared event: sId=0x%x\n", sId);
}
#endif
#endif

/* =====================================================================
 * Kernel-context baseline suite.
 *
 * Like KernelSwtmrTest below, NONE of this is gated on LOSCFG_BOX: it runs in
 * both builds, so it is the A/B evidence that the BOX seams left the kernel
 * caller's view of the baseline API intact.
 *
 * That matters most for the IPC handle FORMAT. The #ifdef LOSCFG_BOX arms in
 * los_mux.c / los_sem.c / los_queue.c are not conditioned on
 * OsGetCurBoxCB(), so under BOX a KERNEL caller also gets a kernel-head sId
 * back instead of a pool index, the control block's own id field carries that
 * sId, and every later Pend/Post/Delete goes through the sId-resolution seam.
 * Before this suite existed nothing in the tree drove those arms from kernel
 * context: board/main.c had no IPC or heap calls at all, and the in-tree
 * kernel callers (los_mp.c / los_cpup.c / los_pm.c) are all compiled out here.
 *
 * Two deliberate omissions:
 *   - LOS_MuxHeld is not used. Under BOX it is declared in the private
 *     los_mux_pri.h only, so referencing it would break the BOX=off build.
 *   - The plain EVENT_CB_S API is not covered: los_event_sid.c is box-only and
 *     the kernel event path carries no BOX seam, so there is nothing to A/B.
 * ===================================================================== */

#if (LOSCFG_BASE_IPC_MUX == 1)
/* create -> recursive pend (muxCount) -> post x2 -> delete -> stale reject */
static UINT32 KernelMuxTest(VOID)
{
    UINT32 mux = 0;
    UINT32 bad = 0;

    if (LOS_MuxCreate(&mux) != LOS_OK) {
        printf("kernel mux create FAILED\n");
        return 1;
    }
    /* recursive acquire: muxCount must reach 2 */
    if ((LOS_MuxPend(mux, LOS_WAIT_FOREVER) != LOS_OK) ||
        (LOS_MuxPend(mux, LOS_NO_WAIT) != LOS_OK)) {
        printf("kernel mux pend FAILED\n");
        bad++;
    }
    /* deleting a still-held mux is refused BY DESIGN in both builds (the
     * pending-owner list is non-empty and muxCount != 0). Asserting the
     * refusal covers an arm the balanced box-side test never reaches. */
    if (LOS_MuxDelete(mux) != LOS_ERRNO_MUX_PENDED) {
        printf("kernel mux delete-while-held not refused\n");
        bad++;
    }
    /* release all the way down to 0, then the delete must go through */
    if ((LOS_MuxPost(mux) != LOS_OK) || (LOS_MuxPost(mux) != LOS_OK)) {
        printf("kernel mux post FAILED\n");
        bad++;
    }
    if (LOS_MuxDelete(mux) != LOS_OK) {
        printf("kernel mux delete FAILED\n");
        bad++;
    }
    /* the handle must stop resolving once deleted, in BOTH builds: under BOX
     * the SID object is unbound, otherwise muxStat goes LOS_UNUSED */
    if (LOS_MuxPend(mux, LOS_NO_WAIT) == LOS_OK) {
        printf("kernel mux stale handle accepted\n");
        bad++;
    }
    return bad;
}
#endif

#if (LOSCFG_BASE_IPC_SEM == 1)
/* count-0 pend must time out, count-1 pend must take it immediately */
static UINT32 KernelSemTest(VOID)
{
    UINT32 sem = 0;
    UINT32 bad = 0;

    if (LOS_SemCreate(0, &sem) != LOS_OK) {
        printf("kernel sem create FAILED\n");
        return 1;
    }
    if (LOS_SemPend(sem, 100) != LOS_ERRNO_SEM_TIMEOUT) {
        printf("kernel sem count-0 pend did not time out\n");
        bad++;
    }
    if ((LOS_SemPost(sem) != LOS_OK) || (LOS_SemPend(sem, LOS_NO_WAIT) != LOS_OK)) {
        printf("kernel sem post/pend FAILED\n");
        bad++;
    }
    if (LOS_SemDelete(sem) != LOS_OK) {
        printf("kernel sem delete FAILED\n");
        bad++;
    }
    if (LOS_SemPend(sem, LOS_NO_WAIT) == LOS_OK) {
        printf("kernel sem stale handle accepted\n");
        bad++;
    }
    return bad;
}
#endif

#if (LOSCFG_BASE_IPC_QUEUE == 1)
/* one write/read roundtrip with a payload check, then delete + stale reject */
static UINT32 KernelQueueTest(VOID)
{
    UINT32 qid = 0;
    UINT32 msgOut = 0xA5A51234U;
    UINT32 msgIn = 0;
    UINT32 readSize = sizeof(UINT32);
    UINT32 bad = 0;

    if (LOS_QueueCreate("kbaseq", 4, &qid, 0, sizeof(UINT32)) != LOS_OK) {
        printf("kernel queue create FAILED\n");
        return 1;
    }
    if ((LOS_QueueWriteCopy(qid, &msgOut, sizeof(UINT32), LOS_NO_WAIT) != LOS_OK) ||
        (LOS_QueueReadCopy(qid, &msgIn, &readSize, LOS_NO_WAIT) != LOS_OK) ||
        (msgIn != msgOut) || (readSize != sizeof(UINT32))) {
        printf("kernel queue roundtrip FAILED (in 0x%x size %u)\n", msgIn, readSize);
        bad++;
    }
    if (LOS_QueueDelete(qid) != LOS_OK) {
        printf("kernel queue delete FAILED\n");
        bad++;
    }
    if (LOS_QueueWriteCopy(qid, &msgOut, sizeof(UINT32), LOS_NO_WAIT) == LOS_OK) {
        printf("kernel queue stale handle accepted\n");
        bad++;
    }
    return bad;
}
#endif

/* Kernel system heap: two live blocks with a written pattern, freed in
 * reverse order, then re-allocated - so the free list is proved to cycle
 * rather than only ever shrink. Under BOX the MEM_LOCK/MEM_UNLOCK macros are
 * redefined onto the per-pool lock family, and the system heap takes the
 * int-lock arm of it; this is what exercises that from kernel context. */
static UINT32 KernelMemTest(VOID)
{
    VOID *blkA;
    VOID *blkB;
    UINT32 i;
    UINT32 bad = 0;

    blkA = LOS_MemAlloc(OS_SYS_MEM_ADDR, 128);
    blkB = LOS_MemAlloc(OS_SYS_MEM_ADDR, 256);
    if ((blkA == NULL) || (blkB == NULL)) {
        printf("kernel heap alloc FAILED (%p %p)\n", blkA, blkB);
        if (blkA != NULL) {
            (VOID)LOS_MemFree(OS_SYS_MEM_ADDR, blkA);
        }
        if (blkB != NULL) {
            (VOID)LOS_MemFree(OS_SYS_MEM_ADDR, blkB);
        }
        return 1;
    }
    for (i = 0; i < (128U / sizeof(UINT32)); i++) {
        ((UINT32 *)blkA)[i] = 0x5A5A0000U | i;
    }
    for (i = 0; i < (128U / sizeof(UINT32)); i++) {
        if (((UINT32 *)blkA)[i] != (0x5A5A0000U | i)) {
            printf("kernel heap pattern mismatch at %u\n", i);
            bad++;
            break;
        }
    }
    if ((LOS_MemFree(OS_SYS_MEM_ADDR, blkB) != LOS_OK) ||
        (LOS_MemFree(OS_SYS_MEM_ADDR, blkA) != LOS_OK)) {
        printf("kernel heap free FAILED\n");
        bad++;
    }
    blkA = LOS_MemAlloc(OS_SYS_MEM_ADDR, 256);
    if (blkA == NULL) {
        printf("kernel heap re-alloc FAILED\n");
        bad++;
    } else if (LOS_MemFree(OS_SYS_MEM_ADDR, blkA) != LOS_OK) {
        printf("kernel heap re-free FAILED\n");
        bad++;
    }
    return bad;
}

#if (LOSCFG_MEM_MUL_POOL == 1)
/* Multi-pool registry walk. Every other registration in the system goes into
 * an EMPTY list - the kernel heap at boot, each box heap from its entry task -
 * so the while-loop in OsMemMulPoolInit (the overlap check, which
 * dereferences each registered pool header via LOS_MemPoolSizeGet) would
 * never execute. Registering a second pool while the kernel heap is already
 * on the list forces that walk.
 *
 * Runs in both builds, so it is also the A/B evidence for the BOX seam: under
 * BOX this takes the KERNEL arm of the per-box dispatch (LOS_IN_KERNEL is
 * true here, so OsMemMulPoolHead returns &g_poolHead). The box arm runs the
 * same walk against a different head, and its dispatch is already proven by
 * boxA/boxB starting at all - the kernel heap is registered before the
 * scheduler starts, so a box whose dispatch picked the kernel list would
 * dereference a kernel pool header from user mode and BusFault on its very
 * first LOS_MemInit. */
#define KMP_SIZE 1024U
static UINT32 g_kmpBuf[KMP_SIZE / sizeof(UINT32)];

static UINT32 KernelMultiPoolTest(VOID)
{
    VOID *poolA = (VOID *)g_kmpBuf;
    VOID *overlap;
    VOID *blk;
    UINT32 bad = 0;

    /* 1. second pool registers -> walks past the kernel heap, no overlap */
    if (LOS_MemInit(poolA, KMP_SIZE) != LOS_OK) {
        printf("kernel multipool init FAILED\n");
        return 1;
    }

    /* 2. the newly registered pool is actually usable */
    blk = LOS_MemAlloc(poolA, 64);
    if (blk == NULL) {
        printf("kernel multipool alloc FAILED\n");
        bad++;
    } else if (LOS_MemFree(poolA, blk) != LOS_OK) {
        printf("kernel multipool free FAILED\n");
        bad++;
    }

    /* 3. a registration overlapping pool A must be rejected. Note this
     *    scribbles: LOS_MemInit writes a lock handle into the address it is
     *    handed BEFORE the overlap check runs (MemLockCreate), so A's payload
     *    area at +512 gets a stale mux id. A's own pool header (memMux at
     *    +8) is untouched, so steps 4-5 still work, and the re-init rebuilds
     *    the whole pool head over the scribble. */
    overlap = (VOID *)((UINTPTR)poolA + (KMP_SIZE / 2U));
    if (LOS_MemInit(overlap, KMP_SIZE / 2U) == LOS_OK) {
        printf("kernel multipool overlap NOT detected\n");
        bad++;
    }

    /* 4. unlink A; 5. re-register it - a stale link would make this an
     *    overlap failure, so passing proves OsMemMulPoolDeinit really
     *    removed the node. */
    if (LOS_MemDeInit(poolA) != LOS_OK) {
        printf("kernel multipool deinit FAILED\n");
        bad++;
    } else if (LOS_MemInit(poolA, KMP_SIZE) != LOS_OK) {
        printf("kernel multipool re-init FAILED\n");
        bad++;
    } else {
        (VOID)LOS_MemDeInit(poolA);
    }
    return bad;
}
#endif /* LOSCFG_MEM_MUL_POOL */

static volatile UINT32 g_kChildRan = 0;

static UINT32 KernelChildTask(UINT32 arg)
{
    (void)arg;
    g_kChildRan++;
    /* Park long enough for the parent to delete us; we are never running at
     * the moment of the delete, so this drives the non-running arm of
     * LOS_TaskDelete (OsTaskStackFree) rather than OsTaskDelActionOnRun. */
    (void)LOS_TaskDelay(2000);
    return LOS_OK;
}

/* Kernel dynamic-task create/delete cycles. Each round allocates a task stack
 * from the kernel heap and a TCB from g_losFreeTask, then returns both. Three
 * rounds in a row only succeed if neither leaks - under BOX that means the
 * OsTaskCreateTCBGet / OsTaskTCBRecycle gates took the kernel arm (boxPtr NULL,
 * encoded id says kernel) instead of routing into a box pool. */
static UINT32 KernelTaskTest(VOID)
{
    TSK_INIT_PARAM_S param = {0};
    UINT32 tid;
    UINT32 round;
    UINT32 bad = 0;

    param.pfnTaskEntry = (TSK_ENTRY_FUNC)KernelChildTask;
    param.usTaskPrio = 4;    /* above HelloworldTask(5), so it runs at once */
    param.pcName = "kernelChild";
    param.uwStackSize = LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE;

    g_kChildRan = 0;
    for (round = 0; round < 3U; round++) {
        tid = 0;
        if (LOS_TaskCreate(&tid, &param) != LOS_OK) {
            printf("kernel task create round %u FAILED\n", round);
            bad++;
            break;
        }
        (void)LOS_TaskDelay(2);        /* let the child run and park */
        if (LOS_TaskDelete(tid) != LOS_OK) {
            printf("kernel task delete round %u FAILED\n", round);
            bad++;
            break;
        }
    }
    if (g_kChildRan != 3U) {
        printf("kernel child ran %u/3\n", g_kChildRan);
        bad++;
    }
    return bad;
}

static VOID KernelBaselineTest(VOID)
{
    UINT32 bad = 0;

#if (LOSCFG_BASE_IPC_MUX == 1)
    bad += KernelMuxTest();
#endif
#if (LOSCFG_BASE_IPC_SEM == 1)
    bad += KernelSemTest();
#endif
#if (LOSCFG_BASE_IPC_QUEUE == 1)
    bad += KernelQueueTest();
#endif
    bad += KernelMemTest();
#if (LOSCFG_MEM_MUL_POOL == 1)
    bad += KernelMultiPoolTest();
#endif
    bad += KernelTaskTest();

    if (bad == 0U) {
        printf("kernel baseline test OK\n");
    } else {
        printf("kernel baseline test FAIL (%u)\n", bad);
    }
}

#ifdef LOSCFG_BASE_CORE_SWTMR
/* Kernel-context software timer coverage.
 *
 * Deliberately NOT gated on LOSCFG_BOX: this runs in BOTH builds, so it is the
 * A/B evidence that the BOX seams in los_swtmr.c left the baseline path alone.
 * Under BOX those seams divert box-owned expiries into a per-box ring; a timer
 * created HERE has no owning box, so OsSwtmrScanDeferUsr must decline it and
 * the kernel swtmr task must run the handler itself - which is the only
 * delivery path a BOX=off build has at all. Called after the box lifecycle
 * block, so under BOX it runs while boxA's suite and boxB's two timers are
 * live: a kernel timer must not be captured by any box ring, and no box timer
 * may be delivered to the kernel task.
 *
 * It also covers what the box-side tests structurally cannot:
 *   - the plain, un-encoded kernel id space (id < LOSCFG_BASE_CORE_SWTMR_LIMIT)
 *   - the kernel pool g_swtmrFreeList instead of a per-box free list
 *   - the stale-handle rotation in OsSwtmrDelete (timerId += SWTMR_LIMIT):
 *     after the delete, OsSwtmrIdVerify's timerId comparison must reject the
 *     old handle. Box pool ids have no equivalent rotation, so this is the
 *     only place that defense gets exercised. */
static volatile UINT32 g_kSwtPeriod = 0;
static volatile UINT32 g_kSwtOnce = 0;
static volatile UINT32 g_kSwtInBox = 0;

static VOID KernelSwtHandler(UINTPTR arg)
{
    g_kSwtPeriod += (UINT32)arg;
#ifdef LOSCFG_BOX
    if (OsGetCurBoxCB() != NULL) {
        g_kSwtInBox++;    /* a kernel timer's callback must never run in a box */
    }
#endif
}

static VOID KernelSwtOnceHandler(UINTPTR arg)
{
    (void)arg;
    g_kSwtOnce++;
}

static VOID KernelSwtmrTest(VOID)
{
    UINT32 idP = 0;
    UINT32 idO = 0;
    UINT32 ret;
    UINT32 i;
    UINT32 bad = 0;

    g_kSwtPeriod = 0;
    g_kSwtOnce = 0;
    g_kSwtInBox = 0;

    /* ---- periodic: kernel pool, handler runs in the kernel swtmr task ---- */
    ret = LOS_SwtmrCreate(10, LOS_SWTMR_MODE_PERIOD, KernelSwtHandler, &idP, (UINTPTR)3);
    if ((ret != LOS_OK) || (LOS_SwtmrStart(idP) != LOS_OK)) {
        printf("kernel swtmr period create/start FAILED 0x%x\n", ret);
        return;
    }
    if (idP >= LOSCFG_BASE_CORE_SWTMR_LIMIT) {
        printf("kernel swtmr id not plain: 0x%x\n", idP);
        bad++;
    }
    for (i = 0; (i < 60U) && (g_kSwtPeriod < 9U); i++) {
        (void)LOS_TaskDelay(2);
    }
    if (g_kSwtPeriod < 9U) {                 /* three firings, arg 3 each */
        printf("kernel swtmr period count %u\n", g_kSwtPeriod);
        bad++;
    }
    if (LOS_SwtmrStop(idP) != LOS_OK) {
        printf("kernel swtmr stop FAILED\n");
        bad++;
    }
    if (LOS_SwtmrDelete(idP) != LOS_OK) {
        printf("kernel swtmr delete FAILED\n");
        bad++;
    }
    /* stale handle: OsSwtmrDelete rotated timerId, so it no longer verifies */
    if (LOS_SwtmrStop(idP) != LOS_ERRNO_SWTMR_NOT_CREATED) {
        printf("kernel swtmr stale id not rejected\n");
        bad++;
    }

    /* ---- one-shot: self-deletes back through the kernel pool ---- */
    ret = LOS_SwtmrCreate(10, LOS_SWTMR_MODE_ONCE, KernelSwtOnceHandler, &idO, 0);
    if ((ret != LOS_OK) || (LOS_SwtmrStart(idO) != LOS_OK)) {
        printf("kernel swtmr once create/start FAILED 0x%x\n", ret);
        bad++;
    } else {
        for (i = 0; (i < 60U) && (g_kSwtOnce == 0U); i++) {
            (void)LOS_TaskDelay(2);
        }
        (void)LOS_TaskDelay(30);             /* ONCE must not re-arm */
        if (g_kSwtOnce != 1U) {
            printf("kernel swtmr once count %u\n", g_kSwtOnce);
            bad++;
        }
        if (LOS_SwtmrDelete(idO) != LOS_ERRNO_SWTMR_NOT_CREATED) {
            printf("kernel swtmr once not self-deleted\n");
            bad++;
        }
    }

    if (g_kSwtInBox != 0U) {
        printf("kernel swtmr handler ran in a box context\n");
        bad++;
    }
    if (bad == 0U) {
        printf("kernel swtmr test OK\n");
    } else {
        printf("kernel swtmr test FAIL (%u)\n", bad);
    }
}
#endif /* LOSCFG_BASE_CORE_SWTMR */

static UINT32 HelloworldTask(UINT32 arg)
{
    (void)arg;
#ifdef LOSCFG_BOX
    /* Box init runs AFTER the scheduler has started (LOS_BoxInit is itself a
     * syscall), so the first task ever dispatched by the scheduler is always
     * a KERNEL task - ArchStartToRun has no user-task branch. The box entry
     * tasks created here enter user mode through the normal PendSV INUSER
     * path, exactly like every later user task. */
#if (LOSCFG_BASE_IPC_EVENT == 1)
    /* Before OsBoxInit on purpose: boxA's first test cycle must already see
     * the published handle, otherwise the cross-read phase would be skipped
     * on the only cycle where the kernel-written bit is still pending. */
    KernelSharedEventSetup();
#endif

    UINT32 ret = OsBoxInit(g_userBoxCBArr, g_sysMemLayoutArr);
    if (ret != LOS_OK) {
        printf("OsBoxInit failed! ERROR: 0x%x\n", ret);
    }

    /* Create/exit churn: boxB's entry task exits on its own, so re-create it
     * from the kernel. boxB's pools hold exactly ONE TCB + ONE pool
     * kernel-stack node, so every cycle strictly depends on the previous
     * exit returning both nodes - a leak of either shows up immediately as
     * TSK_NO_MEMORY on the very next re-create. */
    {
        TSK_INIT_PARAM_S churnParam = {0};
        UINT32 churnTid;
        UINT32 cycle;
        UINT32 fails = 0;

        churnParam.pfnTaskEntry = (TSK_ENTRY_FUNC)boxBChurnEntry;
        churnParam.usTaskPrio = 0;
        churnParam.uwStackSize = LOS_BOXB_STACK_SIZE;   /* user stack size */
        churnParam.pcName = "boxBChurnTask";

        LOS_TaskDelay(2500);    /* boot-time boxB entry: 5 x (print + 300ms), then exit */
        for (cycle = 0; cycle < 5; cycle++) {
            ret = OsFirstBoxTaskCreate(&churnTid, &churnParam, g_boxbStack,
                                       &g_userBoxCBArr[LOS_BOXB_ID]);
            if (ret != LOS_OK) {
                printf("boxB churn FAILED at cycle %u, err 0x%x\n", cycle, ret);
                fails++;
                break;
            }
            LOS_TaskDelay(600);    /* the churn task prints, delays 200ms, exits */
        }
        printf("boxB churn: %u cycles, fails %u\n", cycle, fails);
    }

    /* Box lifecycle: destroy boxB (tear down tasks + reset SID pool) then
     * restart it (rebuild env + recreate the entry task). boxB's entry
     * task prints "Hello boxB!" five times, so a successful restart shows
     * a second burst of those prints. Destroy/restart run from the kernel
     * HelloworldTask (non-boxB context), satisfying OsTaskDestroy's
     * non-current invariant. */
    {
        UINT32 lcRet;
        printf("boxB lifecycle: destroying boxB...\n");
        OsBoxDestroy(LOS_BOXB_ID);
        printf("boxB lifecycle: restarting boxB...\n");
        lcRet = OsBoxRestart(LOS_BOXB_ID);
        if (lcRet != LOS_OK) {
            printf("boxB lifecycle: restart FAILED, err 0x%x\n", lcRet);
        } else {
            printf("boxB lifecycle: restart OK, expect 'Hello boxB!' burst\n");
        }
        LOS_TaskDelay(2000);   /* let the restarted entry task print 5x (300ms each) */
    }
#endif
    /* Outside the LOSCFG_BOX block on purpose - see KernelBaselineTest and
     * KernelSwtmrTest: both must run in the BOX=off build too, that is what
     * makes them A/B evidence rather than box-only tests. */
    KernelBaselineTest();
#ifdef LOSCFG_BASE_CORE_SWTMR
    KernelSwtmrTest();
#endif
    while (1) {
        printf("helloworld\n");
        (void)LOS_TaskDelay(1000);
    }
    return LOS_OK; /* never reached */
}

UINT32 LosAppInit(VOID)
{
    UINT32 ret;
    TSK_INIT_PARAM_S taskInitParam = {0};

    taskInitParam.pfnTaskEntry = (TSK_ENTRY_FUNC)HelloworldTask;
    taskInitParam.usTaskPrio = 5;
    taskInitParam.pcName = "HelloworldTask";
    taskInitParam.uwStackSize = 0x1000;

    ret = LOS_TaskCreate(&g_helloworldTaskId, &taskInitParam);
    return ret;
}
#endif /* !LOSCFG_KERNEL_TEST */

LITE_OS_SEC_TEXT_INIT int main(void)
{
    UINT32 ret;

    UartInit();

#ifdef LOSCFG_BOX
    /* Enable FPU (CP10/CP11 full access) BEFORE any task creation:
     * task contexts are always created in the FPU shape ([d8-d15 64B]
     * [r4-r12 36B][HW frame][FP area 72B], see ArchTaskStackInit), and
     * PendSV only saves/restores that shape when CPACR is on. Enabling
     * it later would leave the created contexts and the switch layout
     * disagreeing (first restore misreads the frame, PC=0 crash). */
    *(volatile UINT32 *)0xE000ED88 |= (0xFU << 20);

    /* FPCCR.ASPEN | FPCCR.LSPEN: automatic + lazy FP state preservation.
     * Lazy is what the syscall path relies on (FPCCR.LSPACT stays pending
     * across the graft, so S0-S15/FPSCR are never written); automatic
     * stacking is what gives every FPCA=1 task an extended frame. */
    *(volatile UINT32 *)0xE000EF34 |= (0x3U << 30);
    __DSB();
    __ISB();
#endif

    ret = LOS_KernelInit();
    if (ret != LOS_OK) {
        printf("LiteOS kernel init failed! ERROR: 0x%x\n", ret);
        goto EXIT;
    }

#ifdef LOSCFG_BOX
    /* Static MPU regions (R0-R2) + box slots armed + SVC vector hook.
     * Kernel keeps running through the PRIVDEFENA background map.
     * OsBoxInit moved into HelloworldTask (after the scheduler starts):
     * the first scheduled task must be a kernel task (see HelloworldTask). */
    MpuLayoutInit();
#endif

    ret = LosAppInit();
    if (ret != LOS_OK) {
        printf("LosAppInit failed! ERROR: 0x%x\n", ret);
    }

    LOS_Start();

EXIT:
    while (1) {
        __asm volatile("wfi");
    }
}

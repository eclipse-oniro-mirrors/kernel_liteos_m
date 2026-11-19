#include "stdio.h"
#include <math.h>            /* frexp - routed into COMMON_TEXT, see liteos.ld */
#if defined(LOSCFG_BOX) && (LOSCFG_BASE_IPC_EVENT == 1)
#include "los_event_sid.h"       /* LosEventCB (pool entity + cfg externs below) */
#endif
#include "boxes_conf.h"
#include "boxes_res_conf.h"
#ifdef LOSCFG_BASE_IPC_CONDVAR
#include "los_condvar_pri.h"     /* LosCondVarCB / OsCondVarXxx (explicit, so the
                                  * test below does not depend on boxes_res_conf.h
                                  * having pulled it in first) */
#endif
#include "los_memory.h"
#include "los_mux.h"
#include "los_sem.h"
#include "los_queue.h"
#include "los_task.h"
#include "los_debug.h"
#include "syscalls.h"
#if defined(LOSCFG_BOX) && (LOSCFG_BASE_IPC_EVENT == 1)
#include "los_event_sid.h"       /* LosEventCB (event pool entity) */
#endif
#ifdef LOSCFG_SHELL
#include "los_shell.h"
#endif

extern VOID LOS_TaskStackRecycle(VOID);

LOS_TASK_STACK_ALIGN UINT8 g_boxaStack[LOS_BOXA_STACK_SIZE]
    __attribute__((section(".boxA.bss"), used));

LITE_OS_SEC_BOX_RESSPACE LosTaskCB     g_boxaTaskCB[LOSCFG_BOXA_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosTaskStack  g_boxaKerStack[LOSCFG_BOXA_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE UINTPTR       g_boxaRecycleStack[LOSCFG_BOXA_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosMuxCB      g_boxaMuxCB[LOSCFG_BOXA_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxaMuxEntity[LOSCFG_BOXA_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosQueueCB    g_boxaQueCB[LOSCFG_BOXA_QUEUE_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxaQueEntity[LOSCFG_BOXA_QUEUE_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosSemCB      g_boxaSemCB[LOSCFG_BOXA_SEM_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxaSemEntity[LOSCFG_BOXA_SEM_LIMIT];
LITE_OS_SEC_BOX_RESSPACE SIdObj        g_boxaSIdPool[LOSCFG_BOXA_SID_LIMIT];
/* This box's timer pool + deferred-callback ring. The
 * queue buffer holds SwtmrHandlerItem values (2 words each). */
LITE_OS_SEC_BOX_RESSPACE LosSwtmrCB  g_boxaSwtCB[LOSCFG_BOXA_SWT_LIMIT];
LITE_OS_SEC_BOX_RESSPACE UINTPTR     g_boxaSwtQueue[LOSCFG_BOXA_SWT_QUEUE_LEN * 2U];
LITE_OS_SEC_BOX_RESSPACE LosEventCB  g_boxaEvtCB[LOSCFG_BOXA_EVENT_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj   g_boxaEvtEntity[LOSCFG_BOXA_EVENT_LIMIT];
#ifdef LOSCFG_BASE_IPC_CONDVAR
LITE_OS_SEC_BOX_RESSPACE LosCondVarCB g_boxaCondCB[LOSCFG_BOXA_COND_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj    g_boxaCondEntity[LOSCFG_BOXA_COND_LIMIT];
#endif

/* Caller-provided (box-static) user stack for the static-create test task —
 * TASK_BASE+1 LOS_TaskCreateStatic path. */
LOS_TASK_STACK_ALIGN STATIC UINT8 g_boxaStaticStack[1024]
    __attribute__((section(".boxA.bss"), used));

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
#include "los_swtmr_usr_pri.h"   /* LosSwtmrCB / BoxSwtmrInit wiring */
#endif
/* current box pool (kernel writes it on task switch; the user world reads
 * it directly - g_boxLocalPool[OS_MEM_POOL_INDEX]) */
extern UINTPTR g_boxLocalPool[];

/* ---- lightweight test harness ----
 * The per-test result prints stay (regression greps target them); the
 * counters add a single go/no-go line per cycle: "boxA summary OK" when
 * g_testFail == 0. Tests count one PASS per completed sub-check group,
 * and every failure prints immediately (name + FAIL). */
STATIC UINT32 g_testPass = 0;
STATIC UINT32 g_testFail = 0;

#define BOX_TEST_OK(name)  do { g_testPass++; } while (0)
#define BOX_TEST_FAIL(name) do { g_testFail++; LOS_PrintStr(name " FAIL\n"); } while (0)
#define BOX_TEST(cond, name) do { if (cond) { g_testPass++; } else { g_testFail++; LOS_PrintStr(name " FAIL\n"); } } while (0)

#ifdef LOSCFG_SHELL
STATIC UINT32 OsShellCmdTest(UINT32 argc, const CHAR **argv)
{
    PRINTK("OsShellCmdTest: boxA\n");
    return 0;
}
#endif
#ifdef TEST_FAULT_DUMP
/* Multi-level fault chain: every level is noinline and disable_tail_calls
 * (LTO turned the inter-level calls into tail calls when the result went
 * unused - which flattens the stack frames the backtrace needs), and the
 * return value is consumed by the caller so post-call work stays live.
 * Level3 holds local buffers as stack noise to exercise the filtering. */
static __attribute__((noinline, disable_tail_calls)) UINT32 BoxAFaultLevel4(volatile UINT32 *p, UINT32 tag)
{
    LOS_PrintStr("boxA: L4, deref NULL now\n");
    UINT32 val = *p; /* MemManage DACCVIOL, MMAR=0 */
    return val ^ tag;
}

static __attribute__((noinline, disable_tail_calls)) UINT32 BoxAFaultLevel3(volatile UINT32 *p, UINT32 tag)
{
    UINT32 buf[4];
    buf[0] = tag;
    buf[1] = tag + 1U;
    buf[2] = tag + 2U;
    buf[3] = tag + 3U;
    LOS_PrintStr("boxA: L3\n");
    UINT32 r = BoxAFaultLevel4(p, buf[0] + buf[1] + buf[2] + buf[3]);
    return r + 1U;
}

static __attribute__((noinline, disable_tail_calls)) UINT32 BoxAFaultLevel2(volatile UINT32 *p, UINT32 tag)
{
    LOS_PrintStr("boxA: L2\n");
    UINT32 r = BoxAFaultLevel3(p, tag * 2U + 1U);
    return r + 2U;
}

static __attribute__((noinline, disable_tail_calls)) UINT32 BoxAFaultLevel1(VOID)
{
    /* Anti-optimization: addr is a volatile local (its value 0 stays
     * opaque to the optimizer, no constant folding), and the pointee
     * is volatile too, so the load in Level4 can never be eliminated
     * under -O2/LTO. boxA runs unprivileged: 0x0 is outside every
     * MPU region, so the load raises MemManage (DACCVIOL, MMAR=0x0). */
    volatile UINTPTR addr = 0;
    volatile UINT32 *nullPtr = (volatile UINT32 *)addr;
    LOS_PrintStr("boxA: L1\n");
    UINT32 r = BoxAFaultLevel2(nullPtr, 0xA5U);
    return r + 3U;
}
#endif /* TEST_FAULT_DUMP */

/* User-mode mutex + memory test (runs unprivileged in boxA):
 *  - mux: create (box pool + box SID), pend/post/held, recursive re-pend
 *    (core behaviour), delete
 *  - mem: alloc from the box heap (syscall -> kernel TLSF under the box
 *    heap's mux lock), write/verify pattern, free */
STATIC VOID BoxUserMuxMemTest(VOID)
{
    UINT32 sId = 0;
    UINT32 ret;
    UINT32 fails = 0;
    VOID *mem = NULL;
    UINT32 i;
    UINT32 patOk = 1;

    ret = LOS_MuxCreate(&sId);
    if (ret != LOS_OK) {
        LOS_PrintStr("mux create FAIL\n");
    } else {
        if (LOS_MuxPend(sId, 100) != LOS_OK) {
            fails++;
        }
        if (LOS_MuxHeld(sId) != 1) {
            fails++;
        }
        if (LOS_MuxPost(sId) != LOS_OK) {
            fails++;
        }
        if (LOS_MuxHeld(sId) != 0) {
            fails++;
        }
        /* recursive re-pend: muxCount path */
        if (LOS_MuxPend(sId, 0) != LOS_OK) {
            fails++;
        }
        if (LOS_MuxPend(sId, 0) != LOS_OK) {
            fails++;
        }
        if (LOS_MuxPost(sId) != LOS_OK) {
            fails++;
        }
        if (LOS_MuxPost(sId) != LOS_OK) {
            fails++;
        }
        if (LOS_MuxDelete(sId) != LOS_OK) {
            fails++;
        }
        if (fails == 0) {
            BOX_TEST_OK("mux user test");
            LOS_PrintStr("mux user test OK\n");
        } else {
            BOX_TEST_FAIL("mux user test");
            LOS_PrintStr("mux user test FAIL\n");
        }
    }

    mem = LOS_MemAlloc((VOID *)g_boxLocalPool[0], 128);
    if (mem == NULL) {
        BOX_TEST_FAIL("mem user test");
        LOS_PrintStr("mem alloc FAIL\n");
        return;
    }
    for (i = 0; i < (128U / sizeof(UINT32)); i++) {
        ((UINT32 *)mem)[i] = 0xA5A50000U | i;
    }
    for (i = 0; i < (128U / sizeof(UINT32)); i++) {
        if (((UINT32 *)mem)[i] != (0xA5A50000U | i)) {
            patOk = 0;
            break;
        }
    }
    if (patOk && (LOS_MemFree((VOID *)g_boxLocalPool[0], mem) == LOS_OK)) {
        BOX_TEST_OK("mem user test");
        LOS_PrintStr("mem user test OK\n");
    } else {
        BOX_TEST_FAIL("mem user test");
        LOS_PrintStr("mem user test FAIL\n");
    }
}

/* User-mode dynamic task test:
 *  - boxA entry task creates a dynamic child via LOS_TaskCreate (user stack
 *    allocated from the box heap, TCB from the box pool).
 *  - the child runs a quick print, returns -> LOS_TaskUsrExit -> the kernel
 *    deletes the detached task; the box heap stack is queued onto
 *    boxResRecycleStackList by OsBoxUserStackRecycle.
 *  - the entry task yields to let idle recycle the TCB, then calls
 *    LOS_TaskStackRecycle to drain + free the stack back to the box heap.
 * boxA has TASK_LIMIT=2 (entry + 1 dynamic slot), so the create must be
 * paired with recycle before the next iteration reuses the slot. */
STATIC volatile UINT32 g_dynChildRan = 0;

STATIC VOID BoxUserDynTaskEntry(VOID)
{
    g_dynChildRan = 1;
    LOS_PrintStr("dyn child running\n");
    /* return -> OsUsrTaskExit trampoline -> LOS_TaskUsrExit -> delete */
}

STATIC UINT32 BoxUserTaskTest(VOID)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S initParam;
    UINT32 ret;

    initParam.pfnTaskEntry = (TSK_ENTRY_FUNC)BoxUserDynTaskEntry;
    initParam.uwStackSize = 1024;
    initParam.pcName = "dynChild";
    initParam.usTaskPrio = 5;
    initParam.uwResved = 0;   /* detached (default): recycled on exit */

    g_dynChildRan = 0;
    ret = LOS_TaskCreate(&taskId, &initParam);
    if (ret != LOS_OK) {
        BOX_TEST_FAIL("dyn create");
        LOS_PrintStr("dyn create FAIL\n");
        return ret;
    }
    /* Query family: InfoGet / StatusGet / PriSet round-trip on the
     * known dyn child (TASK_BASE+9/+10/+14, box-encoded ids via the ID
     * check seam). Runs while the child is alive - independent of when the
     * child actually gets dispatched (first dispatch loses a race on the
     * fast path, see the polling note below). */
    {
        TSK_INFO_S info = {0};
        UINT32 status = 0;
        UINT32 qfails = 0;

        if (LOS_TaskInfoGet(taskId, &info) != LOS_OK) {
            qfails++;
        } else if ((info.uwStackSize != 1024) || (info.uwTaskID != taskId) ||
                   (info.usTaskPrio != 5)) {
            qfails++;
        }
        if (LOS_TaskStatusGet(taskId, &status) != LOS_OK) {
            qfails++;
        } else if (status == 0) {
            qfails++;   /* zero = no state bits at all */
        }
        /* PriSet re-enabled for step 5: the earlier fault was traced to the
         * delay/create number collision (every delay ran the create shim),
         * not to PriSet itself. */
        if (LOS_TaskPriSet(taskId, 6) != LOS_OK) {
            qfails++;
        } else if (LOS_TaskPriGet(taskId) != 6) {
            qfails++;
        }
        /* A1 supplement: prio round-trip back to 5. */
        if (LOS_TaskPriSet(taskId, 5) != LOS_OK) {
            qfails++;
        } else if (LOS_TaskPriGet(taskId) != 5) {
            qfails++;
        }
        BOX_TEST(qfails == 0, "task query test");
        if (qfails != 0) {
            LOS_PrintStr("task query test FAIL\n");
        }
    }
    /* Wait for the child to run: the delay family sleeps properly now
     * (number fix), so the child dispatches on the first yield; a short
     * bounded poll still tolerates a late first dispatch. */
    {
        UINT32 spin;
        for (spin = 0; (g_dynChildRan == 0) && (spin < 20); spin++) {
            LOS_TaskDelay(20);
        }
        if (g_dynChildRan != 1) {
            BOX_TEST_FAIL("dyn child never ran");
            LOS_PrintStr("dyn child never ran\n");
            return LOS_ERRNO_TSK_TIMEOUT;
        }
    }
    /* drain the recycled user stack back to the box heap so the next
     * iteration can reuse both the TCB slot and the heap memory. */
    LOS_TaskStackRecycle();
    BOX_TEST_OK("dyn task test");
    LOS_PrintStr("dyn task test OK\n");
    return LOS_OK;
}

/* User-mode semaphore test (IPC 80..83):
 * create from the box pool (SID handle), pend-timeout on count=0
 * (blocking schedule-out/in), post, immediate pend, delete; repeat once to
 * prove the pool slot is recycled. */
STATIC UINT32 BoxUserSemTest(VOID)
{
    UINT32 sId = 0;
    UINT32 ret;
    UINT32 fails = 0;
    UINT32 round;

    for (round = 0; round < 2; round++) {
        ret = LOS_SemCreate(0, &sId);
        if (ret != LOS_OK) {
            LOS_PrintStr("sem create FAIL\n");
            return ret;
        }
        if (LOS_SemPend(sId, 100) != LOS_ERRNO_SEM_TIMEOUT) {
            fails++;   /* count == 0: full timeout, blocked schedule */
        }
        if (LOS_SemPost(sId) != LOS_OK) {
            fails++;
        }
        if (LOS_SemPend(sId, 0) != LOS_OK) {
            fails++;   /* count == 1: immediate take */
        }
        if (LOS_SemDelete(sId) != LOS_OK) {
            fails++;
        }
    }
    BOX_TEST(fails == 0, "sem user test");
    if (fails == 0) {
        LOS_PrintStr("sem user test OK\n");
    } else {
        LOS_PrintStr("sem user test FAIL\n");
    }
    return LOS_OK;
}

/* Static-create test (TASK_BASE+1, LOS_TaskCreateStatic): the
 * caller-provided user stack lives in box-static memory (g_boxaStaticStack,
 * .boxA.bss) - flagged static, never recycled on delete (unlike the dynamic
 * family). */
STATIC volatile UINT32 g_staticChildRan = 0;

STATIC VOID BoxUserStaticTaskEntry(VOID)
{
    g_staticChildRan = 1;
    LOS_PrintStr("static child running\n");
    /* return -> OsUsrTaskExit trampoline -> delete (detached) */
}

STATIC UINT32 BoxUserStaticTaskTest(VOID)
{
    UINT32 taskId;
    TSK_INIT_PARAM_S initParam;
    UINT32 ret;

    initParam.pfnTaskEntry = (TSK_ENTRY_FUNC)BoxUserStaticTaskEntry;
    initParam.uwStackSize = sizeof(g_boxaStaticStack);
    initParam.pcName = "staticChild";
    initParam.usTaskPrio = 5;
    initParam.uwResved = 0;

    g_staticChildRan = 0;
    ret = LOS_TaskCreateStatic(&taskId, &initParam, (VOID *)g_boxaStaticStack);
    if (ret != LOS_OK) {
        BOX_TEST_FAIL("static create");
        LOS_PrintStr("static create FAIL\n");
        return ret;
    }
    /* Wait for the static child to run - with the dispatch number fix the
     * delay family sleeps properly, so a bounded poll with a couple of
     * yields is plenty (the poll tolerates a late first dispatch). */
    {
        UINT32 spin;
        for (spin = 0; (g_staticChildRan == 0) && (spin < 20); spin++) {
            LOS_TaskDelay(20);
        }
        if (g_staticChildRan != 1) {
            BOX_TEST_FAIL("static child never ran");
            LOS_PrintStr("static child never ran\n");
            return LOS_ERRNO_TSK_TIMEOUT;
        }
    }
    BOX_TEST_OK("static task test");
    LOS_PrintStr("static task test OK\n");
    return LOS_OK;
}

/* Tick + CurTaskPriSet test. The R2-shared tick counter is read
 * directly (usr world, no syscall — the LOS_TickCountGet wrapper naming
 * is deferred, see los_usr_api.c); a measured delay must fall into a
 * sane window. CurTaskPriSet round-trips the CURRENT task priority
 * (restored after the check). */
extern volatile UINT32 g_usrTickCount;

STATIC VOID BoxUserTickTest(VOID)
{
    UINT32 t0 = g_usrTickCount;
    UINT32 fails = 0;
    UINT32 r15 = 0;
    UINT32 r0 = 0;

    LOS_TaskDelay(100);
    {
        UINT32 t1 = g_usrTickCount;
        if ((t1 - t0) < 100) {
            fails++;   /* too fast: the shared counter did not run */
        } else if ((t1 - t0) > 150) {
            fails++;   /* too slow: read a stale/frozen value */
        }
    }
    r15 = LOS_CurTaskPriSet(15);
    r0 = LOS_CurTaskPriSet(0);   /* entry task restores its create-time priority */
    if (r15 != LOS_OK) {
        fails++;
    }
    if (r0 != LOS_OK) {
        fails++;
    }
    BOX_TEST(fails == 0, "tick test");
    if (fails == 0) {
        LOS_PrintStr("tick test OK\n");
    } else {
        LOS_PrintStr("tick test FAIL\n");
    }
}

/* User-world PRINTK test - the common-copy los_printf.c (box usr
 * mem module, COMMON sections). PRINTK expands to VOID LOS_Printf with
 * LOG_COMMON_LEVEL (the path that never touches the kernel-only prefix
 * logic in OsLogLevelCheck); output rides the UartPuts chain directly -
 * no syscall on this path. PRINTK returns void (kernel contract), so the
 * pass/fail line only proves the chain completes; the formatted content
 * is verified by the regression grep (printk marker line + width-padded
 * line). */
STATIC VOID BoxUserPrintKTest(VOID)
{
    PRINTK("printk %s %d %u %x %p %c%%\n", "usr", -42, 42U,
           0xdeadbeefU, (VOID *)0x20011400, 'A');
    PRINTK("printk width [%0100d] tail\n", 7);
    BOX_TEST_OK("printk test");
    LOS_PrintStr("printk test OK\n");
}

#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
/* deferred-callback counter, published by SwtHandler (box domain) */
STATIC volatile UINT32 g_swtmrProbe = 0;

/* Create a 10-tick periodic timer, run it long enough for
 * a few deferred callbacks (the kernel scanner defers into this box's
 * ring; BoxSwtmrTask executes SwtHandler in the box domain), then stop
 * and delete - exercising the whole create/defer/consume/recycle chain. */
STATIC VOID SwtHandler(UINTPTR arg)
{
    STATIC volatile UINT32 swtCount = 0;

    swtCount += (UINT32)arg;
    g_swtmrProbe = swtCount;
}

STATIC VOID BoxUserSwtmrTest(VOID)
{
    UINT32 swtmrId = 0;
    UINT32 ret;
    UINT32 i;

    ret = LOS_SwtmrCreate(10, LOS_SWTMR_MODE_PERIOD, SwtHandler, &swtmrId, (UINTPTR)1);
    if (ret != LOS_OK) {
        BOX_TEST_FAIL("swtmr user test");
        LOS_PrintStr("swtmr create FAIL\n");
        return;
    }
    ret = LOS_SwtmrStart(swtmrId);
    if (ret != LOS_OK) {
        BOX_TEST_FAIL("swtmr user test");
        LOS_PrintStr("swtmr start FAIL\n");
        return;
    }
    for (i = 0; (i < 200) && (g_swtmrProbe < 3); i++) {
        LOS_TaskDelay(2);   /* generous window: 3 callbacks / 30ms */
    }
    (VOID)LOS_SwtmrStop(swtmrId);
    BOX_TEST(g_swtmrProbe >= 3, "swtmr user test");
    if (g_swtmrProbe >= 3) {
        LOS_PrintStr("swtmr user test OK\n");
    }
    (VOID)LOS_SwtmrDelete(swtmrId);
}

/* Deep swtmr coverage on top of BoxUserSwtmrTest: the two one-shot modes,
 * box pool exhaustion, stale-handle rejection, and delete-from-inside-the-
 * handler.
 *
 * A box callback never touches the timer control block: the kernel scanner
 * hands this box's ring a VALUE COPY (handler + arg) and BoxSwtmrTask calls
 * through that copy. Deleting a timer from inside its own handler therefore
 * cannot pull the CB out from under the running callback - phase 3 is the
 * proof. Depending on which side of the kernel swtmr task's inProcess-- the
 * callback happens to run, the delete lands immediately or through the
 * DELETING state; both end with the slot back on the box free list, which is
 * what phase 3 actually asserts (both pool slots become allocatable again
 * with no explicit delete from this task). */
STATIC volatile UINT32 g_swtOnce = 0;
STATIC volatile UINT32 g_swtNoSelf = 0;
STATIC volatile UINT32 g_swtFiller = 0;
STATIC volatile UINT32 g_swtSelfDel = 0;
STATIC volatile UINT32 g_swtSelfId = 0;
STATIC volatile UINT32 g_swtSelfRet = 0;

STATIC VOID SwtOnceHandler(UINTPTR arg)
{
    (VOID)arg;
    g_swtOnce++;
}

STATIC VOID SwtNoSelfHandler(UINTPTR arg)
{
    (VOID)arg;
    g_swtNoSelf++;
}

/* filler for timers that are created but never started: any fire is a bug */
STATIC VOID SwtFillerHandler(UINTPTR arg)
{
    (VOID)arg;
    g_swtFiller++;
}

STATIC VOID SwtSelfDelHandler(UINTPTR arg)
{
    (VOID)arg;
    g_swtSelfDel++;
    if ((g_swtSelfDel == 1U) && (g_swtSelfId != 0U)) {
        g_swtSelfRet = LOS_SwtmrDelete(g_swtSelfId);
    }
}

STATIC VOID BoxUserSwtmrDeepTest(VOID)
{
    UINT32 id1 = 0;
    UINT32 idA = 0;
    UINT32 idB = 0;
    UINT32 idC = 0;
    UINT32 ret;
    UINT32 i;
    UINT32 bad = 0;
    BOOL gotB;
    BOOL gotC;

    g_swtFiller = 0;

    /* ---- phase 1: ONCE fires exactly once, then self-deletes ---- */
    g_swtOnce = 0;
    ret = LOS_SwtmrCreate(5, LOS_SWTMR_MODE_ONCE, SwtOnceHandler, &id1, 0);
    if ((ret != LOS_OK) || (LOS_SwtmrStart(id1) != LOS_OK)) {
        LOS_PrintStr("swtmr deep p1 create/start FAIL\n");
        BOX_TEST_FAIL("swtmr deep test");
        return;
    }
    for (i = 0; (i < 40U) && (g_swtOnce == 0U); i++) {
        LOS_TaskDelay(2);
    }
    LOS_TaskDelay(20);              /* ONCE must not re-arm */
    if (g_swtOnce != 1U) {
        LOS_PrintStr("swtmr deep p1 ONCE count FAIL\n");
        bad++;
    }
    /* Self-deleted, so the slot is UNUSED and the handle must be refused.
     * This runs BEFORE phase 2 re-allocates the slot: a box swtmr id is
     * OS_BOX_SET_ID(owner, in-pool index) and is not rotated on delete, so
     * once the slot is handed out again the old handle is indistinguishable
     * from the new one. */
    if (LOS_SwtmrStop(id1) != LOS_ERRNO_SWTMR_NOT_CREATED) {
        LOS_PrintStr("swtmr deep p1 self-delete FAIL\n");
        bad++;
    }

    /* ---- phase 2: the box pool is finite (LOSCFG_BOXA_SWT_LIMIT) ---- */
    gotB = (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, SwtFillerHandler, &idA, 0) == LOS_OK);
    gotC = (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, SwtFillerHandler, &idB, 0) == LOS_OK);
    if (!gotB || !gotC) {
        LOS_PrintStr("swtmr deep p2 fill FAIL\n");
        bad++;
    }
    idC = 0;
    ret = LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, SwtFillerHandler, &idC, 0);
    if ((ret != LOS_ERRNO_SWTMR_MAXSIZE) || (idC != 0U)) {
        LOS_PrintStr("swtmr deep p2 exhaust FAIL\n");
        bad++;
    }
    /* Free one slot: the refused create must now succeed. Since phase 1 gave
     * its slot back too, this also proves the self-delete recycled into the
     * box free list rather than the kernel one. */
    if (gotB) {
        (VOID)LOS_SwtmrDelete(idA);
    }
    if (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, SwtFillerHandler, &idC, 0) != LOS_OK) {
        LOS_PrintStr("swtmr deep p2 refill FAIL\n");
        bad++;
    } else {
        (VOID)LOS_SwtmrDelete(idC);
    }
    if (gotC) {
        (VOID)LOS_SwtmrDelete(idB);
    }

    /* ---- phase 3: delete the timer from inside its own handler ---- */
    g_swtSelfDel = 0;
    g_swtSelfRet = 0xFFFFFFFFU;
    g_swtSelfId = 0;
    ret = LOS_SwtmrCreate(5, LOS_SWTMR_MODE_PERIOD, SwtSelfDelHandler, &idA, 0);
    if (ret != LOS_OK) {
        LOS_PrintStr("swtmr deep p3 create FAIL\n");
        bad++;
    } else {
        g_swtSelfId = idA;          /* set BEFORE start: the handler reads it */
        if (LOS_SwtmrStart(idA) != LOS_OK) {
            LOS_PrintStr("swtmr deep p3 start FAIL\n");
            bad++;
        }
        for (i = 0; (i < 40U) && (g_swtSelfDel == 0U); i++) {
            LOS_TaskDelay(2);
        }
        LOS_TaskDelay(20);          /* let a deferred delete land */
        if ((g_swtSelfDel < 1U) || (g_swtSelfRet != LOS_OK)) {
            LOS_PrintStr("swtmr deep p3 self-delete FAIL\n");
            bad++;
        }
        /* Both pool slots must be allocatable again with no explicit delete
         * from this task - that is the recycle proof. */
        gotB = (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, SwtFillerHandler, &idB, 0) == LOS_OK);
        gotC = (LOS_SwtmrCreate(10000, LOS_SWTMR_MODE_PERIOD, SwtFillerHandler, &idC, 0) == LOS_OK);
        if (!gotB || !gotC) {
            LOS_PrintStr("swtmr deep p3 recycle FAIL\n");
            bad++;
        }
        if (gotB) {
            (VOID)LOS_SwtmrDelete(idB);
        }
        if (gotC) {
            (VOID)LOS_SwtmrDelete(idC);
        }
        /* Defensive: had the in-handler delete not landed, idA would still
         * be ticking and would flood the ring from here on. Safe in both
         * cases - after the two deletes above the slot is UNUSED again, so a
         * stale idA is simply refused. */
        (VOID)LOS_SwtmrStop(idA);
        (VOID)LOS_SwtmrDelete(idA);
    }

    /* ---- phase 4: NO_SELFDELETE fires once and waits for a manual delete ---- */
    g_swtNoSelf = 0;
    ret = LOS_SwtmrCreate(5, LOS_SWTMR_MODE_NO_SELFDELETE, SwtNoSelfHandler, &idA, 0);
    if ((ret != LOS_OK) || (LOS_SwtmrStart(idA) != LOS_OK)) {
        LOS_PrintStr("swtmr deep p4 create/start FAIL\n");
        bad++;
    } else {
        for (i = 0; (i < 40U) && (g_swtNoSelf == 0U); i++) {
            LOS_TaskDelay(2);
        }
        LOS_TaskDelay(30);          /* must not re-arm */
        if (g_swtNoSelf != 1U) {
            LOS_PrintStr("swtmr deep p4 count FAIL\n");
            bad++;
        }
        /* Alive but no longer ticking: Stop reports NOT_STARTED, whereas an
         * already-recycled timer would report NOT_CREATED. That difference
         * is the whole point of this mode. */
        if (LOS_SwtmrStop(idA) != LOS_ERRNO_SWTMR_NOT_STARTED) {
            LOS_PrintStr("swtmr deep p4 stop FAIL\n");
            bad++;
        }
        if (LOS_SwtmrDelete(idA) != LOS_OK) {
            LOS_PrintStr("swtmr deep p4 delete FAIL\n");
            bad++;
        }
    }

    /* none of the never-started fillers may ever have fired */
    if (g_swtFiller != 0U) {
        LOS_PrintStr("swtmr deep filler fired FAIL\n");
        bad++;
    }

    BOX_TEST(bad == 0U, "swtmr deep test");
    if (bad == 0U) {
        LOS_PrintStr("swtmr deep test OK\n");
    }
}
#endif /* LOSCFG_BOX && LOSCFG_BASE_CORE_SWTMR */

/* SID event-pool test - create from this box's pool, write, poll
 * (read-and-clear), delete (recycle to the pool free list). */
STATIC VOID BoxUserEventTest(VOID)
{
    UINT32 sId = 0;
    UINT32 ret;
    UINT32 val;

    ret = LOS_EventCreate(&sId);
    if (ret != LOS_OK) {
        BOX_TEST_FAIL("event user test");
        LOS_PrintStr("event create FAIL\n");
        return;
    }
    ret = LOS_EventWriteWithSId(sId, 0x11U);
    if (ret != LOS_OK) {
        BOX_TEST_FAIL("event user test");
        LOS_PrintStr("event write FAIL\n");
        return;
    }
    val = LOS_EventPollWithSId(sId, 0x11U, LOS_WAITMODE_OR | LOS_WAITMODE_CLR);
    if (val == 0x11U) {
        BOX_TEST_OK("event user test");
        LOS_PrintStr("event user test OK\n");
    } else {
        BOX_TEST_FAIL("event user test");
        LOS_PrintStr("event poll FAIL\n");
    }
    (VOID)LOS_EventDelete(sId);
}

/* Cross-domain event test: the handle was created by the KERNEL, so it lives
 * in the kernel event table and carries a kernel-head box id. Every call here
 * therefore goes through the kernel-head arm of the two-bound EVT resolve
 * (OsSIdGenObj on the kernel side, OsSIdGetDataCB on ours) from user mode.
 *  - first cycle only: poll back the 0x1 the kernel wrote before publishing
 *    the handle - proves a box sees kernel-written event state;
 *  - every cycle: write 0x2 then poll it back - proves a box can OPERATE on a
 *    kernel-owned event, not merely read it.
 * A missing kernel table would fail both phases with an invalid-sId error,
 * which is the regression this pins down. */
#define BOX_KEVT_KERNEL_MASK 0x1U
#define BOX_KEVT_BOX_MASK    0x2U

/* published by the kernel in R2 (board/main.c, KernelSharedEventSetup).
 * Not compiled under LOSCFG_KERNEL_TEST: the publisher lives in main.c's
 * demo branch (compiled out in test builds) and the demo boxAEntry never
 * runs there (the entry override points at boxTestEntry) - but boxes.ld
 * KEEP retains the whole object, so the reference would be an undefined
 * symbol at link time. */
#ifndef LOSCFG_KERNEL_TEST
extern volatile UINT32 g_kSharedEvtSId;

STATIC VOID BoxUserKernelEventTest(VOID)
{
    STATIC UINT32 crossReadDone = 0;
    UINT32 sId = g_kSharedEvtSId;
    UINT32 val;

    if (sId == 0) {
        BOX_TEST_FAIL("kernel event test");
        LOS_PrintStr("kernel event sId not published\n");
        return;
    }

    if (!crossReadDone) {
        val = LOS_EventPollWithSId(sId, BOX_KEVT_KERNEL_MASK,
                                   LOS_WAITMODE_OR | LOS_WAITMODE_CLR);
        if (val != BOX_KEVT_KERNEL_MASK) {
            BOX_TEST_FAIL("kernel event test");
            LOS_PrintStr("kernel event cross-read FAIL\n");
            return;
        }
        crossReadDone = 1;
    }

    if (LOS_EventWriteWithSId(sId, BOX_KEVT_BOX_MASK) != LOS_OK) {
        BOX_TEST_FAIL("kernel event test");
        LOS_PrintStr("kernel event write FAIL\n");
        return;
    }
    val = LOS_EventPollWithSId(sId, BOX_KEVT_BOX_MASK,
                               LOS_WAITMODE_OR | LOS_WAITMODE_CLR);
    if (val == BOX_KEVT_BOX_MASK) {
        BOX_TEST_OK("kernel event test");
        LOS_PrintStr("kernel event test OK\n");
    } else {
        BOX_TEST_FAIL("kernel event test");
        LOS_PrintStr("kernel event readback FAIL\n");
    }
}
#endif /* !LOSCFG_KERNEL_TEST */

/* Deep test: the BLOCKING half of the SID event family, which the
 * shallow BoxUserEventTest above never reaches - Create/Write/Poll are all
 * non-blocking, so OsSchedWait/OsSchedWake and the timeout path through the
 * SID gate (OsEventGetCB -> this box's flat sId pool) went untested.
 *
 *  phase 1 - cross-task pend + wake: a waiter parks in
 *     LOS_EventReadWithSId(LOS_WAIT_FOREVER); the entry task first confirms
 *     it is STILL parked (state 1, i.e. the read really blocked instead of
 *     polling through), then writes. OsSchedWake resumes the waiter and the
 *     post-wake re-poll hands it the matched mask back.
 *  phase 2 - bounded pend, no writer: must return
 *     LOS_ERRNO_EVENT_READ_TIMEOUT. The read path also has to have consumed
 *     OS_TASK_STATUS_TIMEOUT; a leftover bit would make the next blocking
 *     read in a reused slot return a spurious timeout, and phase 3 reuses
 *     the same slots, so it would show up there.
 *  phase 3 - write-once: two waiters parked on the same mask. WriteOnce
 *     inspects only the HEAD waiter and breaks, so exactly one wakes; the
 *     following plain Write releases the other. Counting wakeups is the only
 *     way to tell the two forms apart - with a single waiter they behave
 *     identically, which is why the box task pool had to grow to 6.
 *
 * Waiters are dynamic box children (LOS_TaskCreate -> box pool TCB + box
 * heap stack, 1024 B each; boxA heap is 0x6be8 so two at once is easy) and
 * every phase recycles before the next one creates. LOS_TaskStackRecycle
 * drains the whole recycle list, so one call covers phase 3's two exits. */
#define BOX_EVT_MASK 0x2U

STATIC volatile UINT32 g_evtSid = 0;
/* 0 = not started, 1 = parked (about to pend / pending), 2 = read returned */
STATIC volatile UINT32 g_evtState[2] = {0, 0};
STATIC volatile UINT32 g_evtResult[2] = {0, 0};

STATIC VOID BoxEvtWaiterRun(UINT32 idx, UINT32 timeout)
{
    g_evtState[idx] = 1;
    g_evtResult[idx] = LOS_EventReadWithSId(g_evtSid, BOX_EVT_MASK,
                                            LOS_WAITMODE_OR | LOS_WAITMODE_CLR, timeout);
    g_evtState[idx] = 2;
    /* return -> OsUsrTaskExit trampoline -> LOS_TaskUsrExit -> delete */
}

STATIC VOID BoxEvtWaiterA(VOID) { BoxEvtWaiterRun(0, LOS_WAIT_FOREVER); }
STATIC VOID BoxEvtWaiterB(VOID) { BoxEvtWaiterRun(1, LOS_WAIT_FOREVER); }
STATIC VOID BoxEvtWaiterT(VOID) { BoxEvtWaiterRun(0, 20); }

STATIC UINT32 BoxEvtSpawn(VOID (*entry)(VOID), CHAR *name, UINT32 *taskId)
{
    TSK_INIT_PARAM_S initParam;

    initParam.pfnTaskEntry = (TSK_ENTRY_FUNC)entry;
    initParam.uwStackSize = 1024;
    initParam.pcName = name;
    initParam.usTaskPrio = 5;
    initParam.uwResved = 0;   /* detached: recycled on exit */
    return LOS_TaskCreate(taskId, &initParam);
}

/* Wait until waiters [0, n) have all PARKED (g_evtState[i] == 1), then report
 * how many never got there.
 *
 * Polls rather than sleeping once. The single fixed LOS_TaskDelay(10) this
 * replaces made "event deep park FAIL" / "event deep pair park FAIL" fire
 * sporadically (three separate occasions on record) and, because g_testFail is
 * never reset, one such round poisoned the boxA summary of EVERY round after
 * it - 1 real failure showed up as 150+ summary FAILs. The waiters are prio 5
 * and so is the helloworld task that runs the boxB destroy/restart churn, so a
 * contended window can eat the whole delay before the child even reaches the
 * read. Polling costs nothing in the common case (it returns on the first tick
 * where all n are parked) and gives ~40 ticks of slack under contention. Same
 * shape as BoxCondParkCheck below. */
STATIC UINT32 BoxEvtWaitParked(UINT32 n)
{
    UINT32 i;
    UINT32 spin;
    UINT32 bad = 0;

    for (spin = 0; spin < 20; spin++) {
        for (i = 0; (i < n) && (g_evtState[i] == 1); i++) {
        }
        if (i == n) {
            break;
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < n; i++) {
        if (g_evtState[i] != 1) {
            bad++;
        }
    }
    return bad;
}

/* Every wait below is a bounded LOS_TaskDelay loop rather than an unbounded
 * one: the entry task is prio 0 and the waiters prio 5, so the waiters only
 * run while it sleeps. A lost wakeup then shows up as a FAIL instead of
 * hanging the whole regression. */
STATIC UINT32 BoxUserEventDeepTest(VOID)
{
    UINT32 sId = 0;
    UINT32 fails = 0;
    UINT32 waitA = 0;
    UINT32 waitB = 0;
    UINT32 spin;

    if (LOS_EventCreate(&sId) != LOS_OK) {
        BOX_TEST_FAIL("event deep test");
        LOS_PrintStr("event deep create FAIL\n");
        return LOS_NOK;
    }
    g_evtSid = sId;

    /* ---- phase 1: blocking pend, released by a cross-task write ---- */
    g_evtState[0] = 0;
    g_evtResult[0] = 0;
    if (BoxEvtSpawn(BoxEvtWaiterA, "evtWakeA", &waitA) != LOS_OK) {
        fails++;
        LOS_PrintStr("event deep spawn A FAIL\n");
    } else {
        if (BoxEvtWaitParked(1) != 0) {
            fails++;                 /* never blocked - the read returned early */
            LOS_PrintStr("event deep park FAIL\n");
        } else if (LOS_EventWriteWithSId(sId, BOX_EVT_MASK) != LOS_OK) {
            fails++;
            LOS_PrintStr("event deep write FAIL\n");
        } else {
            for (spin = 0; (g_evtState[0] != 2) && (spin < 20); spin++) {
                LOS_TaskDelay(2);
            }
            if ((g_evtState[0] != 2) || (g_evtResult[0] != BOX_EVT_MASK)) {
                fails++;             /* not woken, or woken with the wrong mask */
                LOS_PrintStr("event deep wake FAIL\n");
            }
        }
        LOS_TaskDelay(5);              /* let the child exit, idle recycle the TCB */
        LOS_TaskStackRecycle();
    }

    /* ---- phase 2: bounded pend with no writer -> READ_TIMEOUT ---- */
    g_evtState[0] = 0;
    g_evtResult[0] = 0;
    if (BoxEvtSpawn(BoxEvtWaiterT, "evtTmoA", &waitA) != LOS_OK) {
        fails++;
        LOS_PrintStr("event deep spawn T FAIL\n");
    } else {
        for (spin = 0; (g_evtState[0] != 2) && (spin < 40); spin++) {
            LOS_TaskDelay(2);
        }
        if ((g_evtState[0] != 2) || (g_evtResult[0] != LOS_ERRNO_EVENT_READ_TIMEOUT)) {
            fails++;
            LOS_PrintStr("event deep timeout FAIL\n");
        }
        LOS_TaskDelay(5);
        LOS_TaskStackRecycle();
    }

    /* ---- phase 3: WriteOnce wakes the head waiter only ---- */
    g_evtState[0] = 0;
    g_evtState[1] = 0;
    g_evtResult[0] = 0;
    g_evtResult[1] = 0;
    if ((BoxEvtSpawn(BoxEvtWaiterA, "evtOnceA", &waitA) != LOS_OK) ||
        (BoxEvtSpawn(BoxEvtWaiterB, "evtOnceB", &waitB) != LOS_OK)) {
        fails++;
        LOS_PrintStr("event deep spawn pair FAIL\n");
    } else {
        if (BoxEvtWaitParked(2) != 0) {
            fails++;
            LOS_PrintStr("event deep pair park FAIL\n");
        } else if (LOS_EventWriteOnceWithSId(sId, BOX_EVT_MASK) != LOS_OK) {
            fails++;
            LOS_PrintStr("event deep write-once FAIL\n");
        } else {
            /* exactly one woke: 2 (returned) + 1 (still parked) == 3.
             * 2 == neither woke, 4 == once behaved like the plain write.
             * Poll until at least one has returned, THEN settle before judging:
             * polling on "== 3" alone could catch the transient on the way to 4
             * and mask a woke-both bug, while the fixed LOS_TaskDelay(10) this
             * replaces could fire before the woken child was scheduled at all
             * and report a false 2. The settle delay keeps the assertion honest
             * in both directions. */
            for (spin = 0; ((g_evtState[0] + g_evtState[1]) < 3) && (spin < 20); spin++) {
                LOS_TaskDelay(2);
            }
            LOS_TaskDelay(5);
            if ((g_evtState[0] + g_evtState[1]) != 3) {
                fails++;
                LOS_PrintStr("event deep once-count FAIL\n");
            }
            /* the woken reader ran with CLR, so the bit is gone again -
             * write it back to release the survivor. */
            (VOID)LOS_EventWriteWithSId(sId, BOX_EVT_MASK);
            for (spin = 0; ((g_evtState[0] != 2) || (g_evtState[1] != 2)) &&
                           (spin < 20); spin++) {
                LOS_TaskDelay(2);
            }
            if ((g_evtState[0] != 2) || (g_evtState[1] != 2)) {
                fails++;
                LOS_PrintStr("event deep second-wake FAIL\n");
            }
        }
        LOS_TaskDelay(5);
        LOS_TaskStackRecycle();
    }

    (VOID)LOS_EventDelete(sId);
    g_evtSid = 0;
    if (fails == 0) {
        BOX_TEST_OK("event deep test");
        LOS_PrintStr("event deep test OK\n");
        return LOS_OK;
    }
    BOX_TEST_FAIL("event deep test");
    LOS_PrintStr("event deep test FAIL\n");
    return LOS_NOK;
}

/* Soft-double reachability. The box modules are built with
 * -mfpu=fpv4-sp-d16, i.e. a SINGLE-precision FPU, so every double operation
 * compiles to a call into libgcc (__aeabi_dmul / dadd / d2iz / dcmplt ...)
 * rather than to a VFP instruction. Those members now live in COMMON_TEXT
 * (see board/liteos.ld), together with musl's frexp.
 *
 * The point of this test is the CALL, not the arithmetic: if the routing were
 * wrong the branch target would be kernel flash, and an unprivileged fetch
 * there is an IACCVIOL - the box would die loudly. A printed OK is therefore
 * proof of user-mode reachability. The values are volatile so -O2 cannot fold
 * the operations away, and the frexp result is compared with a range (not ==)
 * to keep the double compares (__aeabi_dcmplt/dcmpgt) on the routed path too. */
STATIC VOID BoxUserSoftDoubleTest(VOID)
{
    volatile double da = 1.5;
    volatile double db = 2.25;
    volatile double dc;
    double frac;
    int exp = 0;

    dc = (da * db) + 0.5;              /* 3.875 : dmul + dadd */
    da = dc * 2.0;                     /* 7.75  : dmul */
    if ((INT32)da != 7) {              /*       : d2iz */
        BOX_TEST_FAIL("soft double test");
        LOS_PrintStr("soft double FAIL\n");
        return;
    }
    frac = frexp(7.75, &exp);          /* 0.96875 * 2^3 */
    if ((exp != 3) || (frac < 0.968) || (frac > 0.969)) {
        BOX_TEST_FAIL("soft double test");
        LOS_PrintStr("soft frexp FAIL\n");
        return;
    }
    BOX_TEST_OK("soft double test");
    LOS_PrintStr("soft double test OK\n");
}

/* SID-bound queue - create (the handle comes back SID-encoded), write
 * a payload by value, read it back, delete. Exercises the create-exit SID
 * bind plus the entry normalization gate on both data paths. */
STATIC VOID BoxUserQueueTest(VOID)
{
    UINT32 queueID = 0;
    UINT32 ret;
    UINT32 msgOut = 0xA5A51234U;
    UINT32 msgIn = 0;
    UINT32 readSize = sizeof(UINT32);
    UINT32 fails = 0;

    /* Under BOX the create routes into THIS box's pool (LOSCFG_BOXA_
     * QUEUE_LIMIT == 1) and returns a box-head sId; the kernel baseline
     * suite in main.c proves the kernel pool still serves kernel
     * contexts in parallel. */
    ret = LOS_QueueCreate("boxAq", 4, &queueID, 0, sizeof(UINT32));
    if (ret != LOS_OK) {
        BOX_TEST_FAIL("queue user test");
        LOS_PrintStr("queue create FAIL\n");
        return;
    }

    ret = LOS_QueueWriteCopy(queueID, &msgOut, sizeof(UINT32), LOS_NO_WAIT);
    if (ret != LOS_OK) {
        BOX_TEST_FAIL("queue user test");
        LOS_PrintStr("queue write FAIL\n");
        (VOID)LOS_QueueDelete(queueID);
        return;
    }

    ret = LOS_QueueReadCopy(queueID, &msgIn, &readSize, LOS_NO_WAIT);
    if ((ret != LOS_OK) || (msgIn != msgOut) || (readSize != sizeof(UINT32))) {
        BOX_TEST_FAIL("queue user test");
        LOS_PrintStr("queue read FAIL\n");
        (VOID)LOS_QueueDelete(queueID);
        return;
    }

    /* Box-pool quota: the pool holds exactly ONE queue CB, so a second
     * create must fail (CB_UNAVAILABLE) and a delete must give the slot
     * back. */
    {
        UINT32 q2 = 0;
        UINT32 q3 = 0;

        if (LOS_QueueCreate("boxAq2", 4, &q2, 0, sizeof(UINT32)) == LOS_OK) {
            LOS_PrintStr("queue pool exhaustion FAIL\n");
            fails++;
            (VOID)LOS_QueueDelete(q2);
        }
        if (LOS_QueueDelete(queueID) != LOS_OK) {
            LOS_PrintStr("queue delete FAIL\n");
            fails++;
        }
        if (LOS_QueueCreate("boxAq3", 4, &q3, 0, sizeof(UINT32)) != LOS_OK) {
            LOS_PrintStr("queue slot recycle FAIL\n");
            fails++;
        } else {
            (VOID)LOS_QueueDelete(q3);
        }
    }

    if (fails == 0) {
        BOX_TEST_OK("queue user test");
        LOS_PrintStr("queue user test OK\n");
    } else {
        BOX_TEST_FAIL("queue user test");
        LOS_PrintStr("queue user test FAIL\n");
    }
}

#ifdef LOSCFG_BASE_IPC_CONDVAR
/* ---- condvar: the fifth SID module ----
 * A condvar is a pool object addressed by sId, built out of an embedded
 * EVENT_CB_S plus a mutex sId. The event layer underneath is already covered by
 * the event tests, so these four phases target what is specific to condvar:
 *   1. a parked waiter is woken by signal, and the wait returns LOS_OK having
 *      re-taken the mutex on the way out;
 *   2. broadcast wakes EVERY parked waiter where signal wakes only the head -
 *      two waiters are the only way to tell them apart, the same discipline the
 *      event WriteOnce test uses;
 *   3. a bounded wait with no signaler times out AND rolls its waiter count
 *      back. Proved indirectly: OsCondVarDelete refuses a condvar whose count
 *      is still positive, so a successful delete is the evidence;
 *   4. delete returns both the pool node and the SID slot, so a fresh create
 *      succeeds afterwards (boxA has only COND_LIMIT=2 nodes).
 * Contract under test: the waiter must HOLD the mutex before waiting, and
 * OsCondVarWait releases it around the wait and re-takes it before returning.
 * Waits are bounded LOS_TaskDelay loops, not unbounded ones - the entry task is
 * prio 0 and the waiters prio 5, so a lost wakeup must show up as a FAIL rather
 * than hang the regression. */
STATIC volatile UINT32 g_condSid = 0;
STATIC volatile UINT32 g_condMux = 0;
/* 0 = not started, 1 = parked inside OsCondVarWait, 2 = the wait returned */
STATIC volatile UINT32 g_condState[2] = {0, 0};
STATIC volatile UINT32 g_condResult[2] = {0, 0};

STATIC VOID BoxCondWaiterBody(UINT32 idx, UINT32 ticks)
{
    if (LOS_MuxPend(g_condMux, LOS_WAIT_FOREVER) != LOS_OK) {
        g_condResult[idx] = LOS_NOK;
        g_condState[idx] = 2;
        return;
    }
    g_condState[idx] = 1;
    g_condResult[idx] = OsCondVarWait(g_condSid, g_condMux, ticks);
    g_condState[idx] = 2;
    (VOID)LOS_MuxPost(g_condMux);
    /* return -> OsUsrTaskExit trampoline -> LOS_TaskUsrExit -> delete */
}

STATIC VOID BoxCondWaiterA(VOID) { BoxCondWaiterBody(0, LOS_WAIT_FOREVER); }
STATIC VOID BoxCondWaiterB(VOID) { BoxCondWaiterBody(1, LOS_WAIT_FOREVER); }

/* Park waiters [0, n) and check each one really blocked. Returns the number
 * that failed to park.
 *
 * Polls instead of sleeping once. A single fixed LOS_TaskDelay(10) made phase 2
 * (two waiters) fail once in 184 rounds, and the log put it exactly inside the
 * kernel's boxB destroy/restart window: that churn runs in the helloworld task
 * at the SAME priority as these waiters, so it can eat the whole delay and the
 * second waiter has not reached OsCondVarWait yet when the check runs. Polling
 * costs nothing in the common case (it returns as soon as all n are parked) and
 * gives 40 ticks of slack when the CPU is contended. Same shape as
 * BoxCondWakeCheck below. */
STATIC UINT32 BoxCondParkCheck(UINT32 n)
{
    UINT32 i;
    UINT32 spin;
    UINT32 bad = 0;

    for (spin = 0; spin < 20; spin++) {
        for (i = 0; (i < n) && (g_condState[i] == 1); i++) {
        }
        if (i == n) {
            break;
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < n; i++) {
        if (g_condState[i] != 1) {
            bad++;
        }
    }
    return bad;
}

/* Wait for waiters [0, n) to leave OsCondVarWait, all with LOS_OK. */
STATIC UINT32 BoxCondWakeCheck(UINT32 n)
{
    UINT32 i;
    UINT32 spin;
    UINT32 bad = 0;

    for (spin = 0; spin < 20; spin++) {
        for (i = 0; (i < n) && (g_condState[i] == 2); i++) {
        }
        if (i == n) {
            break;
        }
        LOS_TaskDelay(2);
    }
    for (i = 0; i < n; i++) {
        if ((g_condState[i] != 2) || (g_condResult[i] != LOS_OK)) {
            bad++;
        }
    }
    return bad;
}

STATIC UINT32 BoxUserCondVarTest(VOID)
{
    UINT32 condSid = 0;
    UINT32 muxId = 0;
    UINT32 fails = 0;
    UINT32 waitA = 0;
    UINT32 waitB = 0;
    UINT32 sret;
    BOOL condAlive = FALSE;

    if (LOS_MuxCreate(&muxId) != LOS_OK) {
        BOX_TEST_FAIL("condvar test");
        LOS_PrintStr("condvar mux create FAIL\n");
        return LOS_NOK;
    }
    /* OsCondVarCreate's argument is IN/OUT: 0 means "allocate a fresh sId", a
     * non-zero value means "bind to this pre-generated slot" (refs shape). */
    condSid = 0;
    if (OsCondVarCreate(&condSid) != LOS_OK) {
        fails++;
        LOS_PrintStr("condvar create FAIL\n");
    } else {
        condAlive = TRUE;
    }
    g_condSid = condSid;
    g_condMux = muxId;

    /* ---- phase 1: one parked waiter, woken by signal ----
     * The parent HOLDS the mutex while signalling. That is not decoration:
     * BoxCondWaiterBody sets g_condState[] = 1 after LOS_MuxPend but BEFORE
     * OsCondVarWait increments cond->count, so a signal sent on the strength of
     * state == 1 alone can land in that window, see count <= 0, return LOS_OK
     * without waking anyone, and leave the waiter parked forever. Holding the
     * mutex closes the window structurally - the parent can only acquire it
     * after OsCondVarWait released it, which happens after the count bump. This
     * is the POSIX contract refs' OsCondVarWait is built around. */
    if (fails == 0) {
        g_condState[0] = 0;
        g_condResult[0] = 0;
        if (BoxEvtSpawn(BoxCondWaiterA, "condWakeA", &waitA) != LOS_OK) {
            fails++;
            LOS_PrintStr("condvar spawn A FAIL\n");
        } else if (BoxCondParkCheck(1) != 0) {
            fails++;
            LOS_PrintStr("condvar park FAIL\n");
        } else if (LOS_MuxPend(muxId, LOS_WAIT_FOREVER) != LOS_OK) {
            fails++;
            LOS_PrintStr("condvar p1 pend FAIL\n");
        } else {
            sret = OsCondVarSignal(condSid, FALSE);
            (VOID)LOS_MuxPost(muxId);
            if (sret != LOS_OK) {
                fails++;
                LOS_PrintStr("condvar signal FAIL\n");
            } else if (BoxCondWakeCheck(1) != 0) {
                fails++;
                LOS_PrintStr("condvar wake FAIL\n");
            }
        }
        LOS_TaskDelay(5);              /* let the child exit, idle recycle the TCB */
        LOS_TaskStackRecycle();
    }

    /* ---- phase 2: two parked waiters, both woken by broadcast ---- */
    if (fails == 0) {
        g_condState[0] = 0;
        g_condState[1] = 0;
        g_condResult[0] = 0;
        g_condResult[1] = 0;
        if ((BoxEvtSpawn(BoxCondWaiterA, "condBcA", &waitA) != LOS_OK) ||
            (BoxEvtSpawn(BoxCondWaiterB, "condBcB", &waitB) != LOS_OK)) {
            fails++;
            LOS_PrintStr("condvar spawn BC FAIL\n");
        } else if (BoxCondParkCheck(2) != 0) {
            fails++;                   /* both must be parked before a broadcast
                                        * is distinguishable from a signal */
            LOS_PrintStr("condvar park2 FAIL\n");
        } else if (LOS_MuxPend(muxId, LOS_WAIT_FOREVER) != LOS_OK) {
            fails++;
            LOS_PrintStr("condvar p2 pend FAIL\n");
        } else {
            sret = OsCondVarSignal(condSid, TRUE);
            (VOID)LOS_MuxPost(muxId);
            if (sret != LOS_OK) {
                fails++;
                LOS_PrintStr("condvar broadcast FAIL\n");
            } else if (BoxCondWakeCheck(2) != 0) {
                fails++;               /* broadcast woke only the head waiter */
                LOS_PrintStr("condvar wake2 FAIL\n");
            }
        }
        LOS_TaskDelay(5);
        LOS_TaskStackRecycle();
    }

    /* ---- phase 3: bounded wait, no signaler -> timeout + count rollback ---- */
    if (fails == 0) {
        UINT32 wret;

        if (LOS_MuxPend(muxId, LOS_WAIT_FOREVER) != LOS_OK) {
            fails++;
            LOS_PrintStr("condvar phase3 pend FAIL\n");
        } else {
            wret = OsCondVarWait(condSid, muxId, 20);
            (VOID)LOS_MuxPost(muxId);
            if (wret == LOS_OK) {
                fails++;               /* nobody signalled, so it must not succeed */
                LOS_PrintStr("condvar timeout FAIL\n");
            }
        }
        /* OsCondVarDelete refuses a condvar with waiters still registered, so a
         * clean delete here is the evidence that the timed-out wait rolled its
         * count back (OsCountSub). */
        if (OsCondVarDelete(condSid) != LOS_OK) {
            fails++;
            LOS_PrintStr("condvar delete FAIL\n");
        } else {
            condAlive = FALSE;
        }
    }

    /* ---- phase 4: the pool node and the SID slot both came back ---- */
    if (fails == 0) {
        condSid = 0;
        if (OsCondVarCreate(&condSid) != LOS_OK) {
            fails++;
            LOS_PrintStr("condvar recreate FAIL\n");
        } else {
            condAlive = TRUE;
            if (OsCondVarDelete(condSid) != LOS_OK) {
                fails++;
                LOS_PrintStr("condvar recreate del FAIL\n");
            } else {
                condAlive = FALSE;
            }
        }
    }

    /* Cleanup runs regardless of `fails`, and it is what keeps one jittery
     * round from poisoning every round after it: a waiter left parked holds the
     * condvar's count positive, OsCondVarDelete then refuses it, and the pool
     * node leaks - with boxA COND_LIMIT=2, two leaked rounds make every later
     * create fail (observed as 310 consecutive "condvar create FAIL" in the 50s
     * regression after a single lost-wakeup round). Broadcast releases any
     * parked waiter, the delay lets it leave OsCondVarWait and exit, and only
     * then is the delete guaranteed to be accepted. */
    if (condAlive) {
        (VOID)OsCondVarSignal(condSid, TRUE);
        LOS_TaskDelay(10);
        LOS_TaskStackRecycle();
        (VOID)OsCondVarDelete(condSid);
    }
    (VOID)LOS_MuxDelete(muxId);

    if (fails == 0) {
        BOX_TEST_OK("condvar test");
        LOS_PrintStr("condvar test OK\n");
    } else {
        BOX_TEST_FAIL("condvar test");
        LOS_PrintStr("condvar test FAIL\n");
    }
    return (fails == 0) ? LOS_OK : LOS_NOK;
}
#endif /* LOSCFG_BASE_IPC_CONDVAR */

VOID boxAEntry(VOID)
{
    /* The box heap is brought up BY the entry task (user mode,
     * LOS_MemInit -> LOS_MemPoolInit veneer); a restart re-runs this and
     * re-registers the pool (OsBoxDestroy unlinked it via
     * OsBoxMulPoolHeadClear). */
    UINT32 ret = LOS_MemInit((VOID *)OS_BOXA_POOL_ADDR, (UINT32)BOXA_HEAP_SIZE);
    if (ret != LOS_OK) {
        LOS_PrintStr("LOS_MemInit init Failed!\n");
        return;
    }
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
    /* Bring up this box's swtmr task (the deferred-callback
     * consumer). Runs in box context so the task create routes into this
     * box's pools. */
    ret = BoxSwtmrInit(LOS_BOXA_ID, 1024);
    if (ret != LOS_OK) {
        LOS_PrintStr("BoxSwtmrInit Failed!\n");
    }
#endif
    while (1) {
        LOS_PrintStr("Hello boxA!\n");
        UINT32 r = LOS_OffsetTest(1, 2, 3, 4, 5, 6, 7, 8);
        if (r == 0x12345678U) {
            BOX_TEST_OK("OffsetTest");
            LOS_PrintStr("OffsetTest 8-arg OK\n");
        } else {
            BOX_TEST_FAIL("OffsetTest");
            LOS_PrintStr("OffsetTest FAIL\n");
        }
#ifdef TEST_FAULT_DUMP
        /* The chain result must be consumed: an unused return value lets
         * LTO dead-code the post-call arithmetic and re-create tail calls
         * even with disable_tail_calls on the callees. */
        LOS_PrintStr("boxA: entering 4-level fault chain\n");
        UINT32 chain = BoxAFaultLevel1();
        if (chain == 0x12345678U) { /* unreachable: L4 faults first */
            LOS_PrintStr("boxA: chain returned, unexpected!\n");
        }

#endif
        BoxUserMuxMemTest();
        /* step 5 (number fix): full feature set again - the delay/create
         * dispatch collision was the root of the late dispatch + fault. */
        (VOID)BoxUserSemTest();
        (VOID)BoxUserTaskTest();
        (VOID)BoxUserStaticTaskTest();
        BoxUserTickTest();
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
        BoxUserSwtmrTest();
        BoxUserSwtmrDeepTest();
#endif
        BoxUserEventTest();
        (VOID)BoxUserEventDeepTest();
#ifndef LOSCFG_KERNEL_TEST
        BoxUserKernelEventTest();
#endif
        BoxUserQueueTest();
#ifdef LOSCFG_BASE_IPC_CONDVAR
        (VOID)BoxUserCondVarTest();
#endif
        BoxUserPrintKTest();
        /* FPU test: hard-float ABI, non-volatile accumulators, so the
         * values must live in S registers across the syscall (lazy: the
         * kernel writes nothing) and across the task switch (PendSV
         * saves S0-S31 + FPSCR). State loss shows up as a wrong result. */
        float fa = 1.5f;
        float fb = 2.25f;
        int i;
        for (i = 0; i < 2; i++) {
            fa = fa * fb + 0.5f;          /* 3.875, then 9.21875 */
            LOS_TaskDelay(1);             /* syscall + task switch */
        }
        fa = fa * 2.0f + 0.125f;          /* 18.5625 */
        if ((fa > 18.56f) && (fa < 18.57f)) {
            BOX_TEST_OK("FPU test");
            LOS_PrintStr("FPU test OK\n");
        } else {
            BOX_TEST_FAIL("FPU test");
            LOS_PrintStr("FPU test FAIL\n");
        }
        /* Single-precision above runs on the VFP hardware; this one
         * goes through the libgcc soft-double helpers in COMMON_TEXT. */
        BoxUserSoftDoubleTest();
        /* Harness: one go/no-go line per cycle. */
        if (g_testFail == 0) {
            LOS_PrintStr("boxA summary OK\n");
        } else {
            LOS_PrintStr("boxA summary FAIL\n");
        }

        LOS_TaskDelay(1000);
    }
}

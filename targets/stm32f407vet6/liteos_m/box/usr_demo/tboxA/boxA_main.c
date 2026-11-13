#include "stdio.h"
#include "boxes_conf.h"
#include "boxes_res_conf.h"
#include "los_memory.h"
#include "los_mux.h"
#include "los_sem.h"
#include "los_task.h"
#include "los_debug.h"
#include "syscalls.h"
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

/* M5G: caller-provided (box-static) user stack for the static-create
 * test task — TASK_BASE+1 LOS_TaskCreateStatic path. */
LOS_TASK_STACK_ALIGN STATIC UINT8 g_boxaStaticStack[1024]
    __attribute__((section(".boxA.bss"), used));

/* current box pool (kernel writes it on task switch; refs usr world reads
 * it directly - g_boxLocalPool[OS_MEM_POOL_INDEX]) */
extern UINTPTR g_boxLocalPool[];

/* ---- M5G lightweight test harness ----
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

/* M5C user-mode mutex + memory test (runs unprivileged in boxA):
 *  - mux: create (box pool + box SID), pend/post/held, recursive re-pend
 *    (OHOS core), delete
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

/* M5D user-mode dynamic task test:
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
    /* M5G query family (A1): InfoGet / StatusGet / PriSet round-trip on the
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

/* M5G user-mode semaphore test (IPC 80..83, refs safe_ipc sem shape):
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

/* M5G static-create test (TASK_BASE+1, refs LOS_TaskCreateStatic): the
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

/* M5G: tick + CurTaskPriSet test. The R2-shared tick counter is read
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

/* M5G-R: user-world PRINTK test - the common-copy los_printf.c (box usr
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

VOID boxAEntry(VOID)
{
    /* refs demo: the box heap is brought up BY the entry task (user mode,
     * LOS_MemInit -> LOS_MemPoolInit veneer); a restart re-runs this and
     * re-registers the pool (OsBoxDestroy unlinked it via
     * OsBoxMulPoolHeadClear). */
    UINT32 ret = LOS_MemInit((VOID *)OS_BOXA_POOL_ADDR, (UINT32)BOXA_HEAP_SIZE);
    if (ret != LOS_OK) {
        LOS_PrintStr("LOS_MemInit init Failed!\n");
        return;
    }
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
        /* M5G harness: one go/no-go line per cycle. */
        if (g_testFail == 0) {
            LOS_PrintStr("boxA summary OK\n");
        } else {
            LOS_PrintStr("boxA summary FAIL\n");
        }

        LOS_TaskDelay(1000);
    }
}

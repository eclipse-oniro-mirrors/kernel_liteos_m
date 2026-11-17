#include "stdio.h"
#if defined(LOSCFG_BOX) && (LOSCFG_BASE_IPC_EVENT == 1)
#include "los_event_sid.h"       /* LosEventCB (cfg externs below) */
#endif
#include "boxes_conf.h"
#include "boxes_res_conf.h"
#include "los_memory.h"
#include "los_debug.h"
#include "syscalls.h"
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
#include "los_swtmr_usr_pri.h"   /* LosSwtmrCB / BoxSwtmrInit wiring */
#endif

LOS_TASK_STACK_ALIGN UINT8 g_boxbStack[LOS_BOXB_STACK_SIZE]
    __attribute__((section(".boxB.bss"), used));

LITE_OS_SEC_BOX_RESSPACE LosTaskCB     g_boxbTaskCB[LOSCFG_BOXB_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosTaskStack  g_boxbKerStack[LOSCFG_BOXB_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE UINTPTR       g_boxbRecycleStack[LOSCFG_BOXB_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosMuxCB      g_boxbMuxCB[LOSCFG_BOXB_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxbMuxEntity[LOSCFG_BOXB_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE SIdObj        g_boxbSIdPool[LOSCFG_BOXB_SID_LIMIT];
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
/* This box's timer pool and deferred-callback ring. The ring buffer holds
 * SwtmrHandlerItem values, 2 words each. */
LITE_OS_SEC_BOX_RESSPACE LosSwtmrCB    g_boxbSwtCB[LOSCFG_BOXB_SWT_LIMIT];
LITE_OS_SEC_BOX_RESSPACE UINTPTR       g_boxbSwtQueue[LOSCFG_BOXB_SWT_QUEUE_LEN * 2U];

/* Two timers with two DISTINCT handlers and a non-unit arg, so a mis-delivered
 * ring item - wrong handler, wrong arg, or one timer's callback credited to the
 * other - shows up as a wrong counter instead of a pass. */
STATIC volatile UINT32 g_boxbSwtA = 0;
STATIC volatile UINT32 g_boxbSwtB = 0;
STATIC volatile UINT32 g_boxbSwtBn = 0;

STATIC VOID BoxbSwtHandlerA(UINTPTR arg)
{
    (VOID)arg;
    g_boxbSwtA++;
}

/* arg carries 100, so g_boxbSwtB must stay exactly 100 * the firing count */
STATIC VOID BoxbSwtHandlerB(UINTPTR arg)
{
    g_boxbSwtBn++;
    g_boxbSwtB += (UINT32)arg;
}
#endif /* LOSCFG_BOX && LOSCFG_BASE_CORE_SWTMR */

VOID boxBEntry(VOID)
{
    /* The box heap is brought up BY the entry task (user mode, LOS_MemInit
     * -> LOS_MemPoolInit veneer); a restart re-runs this and re-registers
     * the pool (OsBoxDestroy unlinked it via OsBoxMulPoolHeadClear).
     * Lifecycle exercise: print 5 rounds, then simply RETURN - the LR
     * trampoline (OsUsrTaskExit) terminates the task through the encoded-id
     * delete path: TCB back to the box pool, kernel stack back to the box
     * stack pool (delayed recycle), user stack not recycled (static). */
    UINT32 ret = LOS_MemInit((VOID *)OS_BOXB_POOL_ADDR, (UINT32)BOXB_HEAP_SIZE);
    int i;

    if (ret != LOS_OK) {
        LOS_PrintStr("LOS_MemInit init Failed!\n");
        return;
    }
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
    /* Bring up this box's swtmr task (the deferred-callback consumer). Runs
     * in box context so the create routes into this box's pools. It takes the
     * second of the two task slots permanently, which is what keeps the churn
     * in boxBChurnEntry down to exactly one free slot. */
    ret = BoxSwtmrInit(LOS_BOXB_ID, 1024);
    if (ret != LOS_OK) {
        PRINTK("boxB BoxSwtmrInit FAIL 0x%x\n", ret);
    }

    /* Two periodic timers, started here and LEFT RUNNING when this entry task
     * returns below:
     *   - a timer belongs to the box, not to the task that created it, so they
     *     must keep firing after the creator is gone;
     *   - the kernel-side destroy in main.c tears this box down with both
     *     timers TICKING, which is the only path that reaches
     *     OsSwtmrDestroyUsr's sortlink-unlink branch;
     *   - the restart re-runs this entry, so both creates must succeed a
     *     second time - that is the proof the destroy recycled the pool
     *     instead of leaking it.
     * The 200-tick interval makes the print loop below double as the wait. */
    g_boxbSwtA = 0;
    g_boxbSwtB = 0;
    g_boxbSwtBn = 0;
    {
        UINT32 idA = 0;
        UINT32 idB = 0;
        UINT32 r1;
        UINT32 r2;
        UINT32 r3;
        UINT32 r4;

        r1 = LOS_SwtmrCreate(200, LOS_SWTMR_MODE_PERIOD, BoxbSwtHandlerA, &idA, 0);
        r2 = LOS_SwtmrCreate(200, LOS_SWTMR_MODE_PERIOD, BoxbSwtHandlerB, &idB, (UINTPTR)100);
        r3 = LOS_SwtmrStart(idA);
        r4 = LOS_SwtmrStart(idB);
        if ((r1 != LOS_OK) || (r2 != LOS_OK) || (r3 != LOS_OK) || (r4 != LOS_OK)) {
            PRINTK("boxB swtmr create/start FAIL %x %x %x %x\n", r1, r2, r3, r4);
        }
    }
#endif
    for (i = 0; i < 5; i++) {
        LOS_PrintStr("Hello boxB!\n");
        LOS_TaskDelay(300);
    }
#if defined(LOSCFG_BOX) && defined(LOSCFG_BASE_CORE_SWTMR)
    /* 1500 ticks of print loop at a 200-tick interval: ~7 firings each */
    if ((g_boxbSwtA >= 2U) && (g_boxbSwtBn >= 2U) && (g_boxbSwtB == (g_boxbSwtBn * 100U))) {
        LOS_PrintStr("boxB swtmr test OK\n");
    } else {
        LOS_PrintStr("boxB swtmr test FAIL\n");
    }
#endif
}

VOID boxBChurnEntry(VOID)
{
    /* Churn regression: one print + one delay, then return - the exit path
     * must return BOTH the pool TCB and the pool kernel-stack node, or the
     * next re-create fails. The box swtmr task holds the other task slot for
     * good, so this entry still cycles through exactly one TCB and one
     * kernel-stack node - a leak of either is caught just as tightly as when
     * the pool had a single slot in total. */
    LOS_PrintStr("boxB churn cycle\n");
    LOS_TaskDelay(200);
}

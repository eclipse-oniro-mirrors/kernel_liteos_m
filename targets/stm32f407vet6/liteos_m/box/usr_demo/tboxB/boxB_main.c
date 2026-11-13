#include "stdio.h"
#include "boxes_conf.h"
#include "boxes_res_conf.h"
#include "los_memory.h"
#include "syscalls.h"

LOS_TASK_STACK_ALIGN UINT8 g_boxbStack[LOS_BOXB_STACK_SIZE]
    __attribute__((section(".boxB.bss"), used));

LITE_OS_SEC_BOX_RESSPACE LosTaskCB     g_boxbTaskCB[LOSCFG_BOXB_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosTaskStack  g_boxbKerStack[LOSCFG_BOXB_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE UINTPTR       g_boxbRecycleStack[LOSCFG_BOXB_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosMuxCB      g_boxbMuxCB[LOSCFG_BOXB_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxbMuxEntity[LOSCFG_BOXB_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE SIdObj        g_boxbSIdPool[LOSCFG_BOXB_SID_LIMIT];

VOID boxBEntry(VOID)
{
    /* refs demo: the box heap is brought up BY the entry task (user mode,
     * LOS_MemInit -> LOS_MemPoolInit veneer); a restart re-runs this and
     * re-registers the pool (OsBoxDestroy unlinked it via
     * OsBoxMulPoolHeadClear).
     * M5B lifecycle exercise: print 5 rounds, then simply RETURN - the LR
     * trampoline (OsUsrTaskExit) terminates the task through the encoded-id
     * delete path: TCB back to the box pool, kernel stack back to the box
     * stack pool (delayed recycle), user stack not recycled (static). */
    UINT32 ret = LOS_MemInit((VOID *)OS_BOXB_POOL_ADDR, (UINT32)BOXB_HEAP_SIZE);
    int i;

    if (ret != LOS_OK) {
        LOS_PrintStr("LOS_MemInit init Failed!\n");
        return;
    }
    for (i = 0; i < 5; i++) {
        LOS_PrintStr("Hello boxB!\n");
        LOS_TaskDelay(300);
    }
}

VOID boxBChurnEntry(VOID)
{
    /* M5B-R churn regression: one print + one delay, then return - the exit
     * path must return BOTH the pool TCB and the pool kernel-stack node
     * (boxB has exactly one of each), or the next re-create fails. */
    LOS_PrintStr("boxB churn cycle\n");
    LOS_TaskDelay(200);
}

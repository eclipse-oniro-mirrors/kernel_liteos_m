#include "stdio.h"
#include "boxes_conf.h"
#include "boxes_res_conf.h"
#include "los_memory.h"
#include "syscalls.h"
#ifdef LOSCFG_SHELL
#include "los_shell.h"
#endif

LOS_TASK_STACK_ALIGN UINT8 g_boxaStack[LOS_BOXA_STACK_SIZE]
    __attribute__((section(".boxA.bss"), used));

LITE_OS_SEC_BOX_RESSPACE LosTaskCB     g_boxaTaskCB[LOSCFG_BOXA_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosTaskStack  g_boxaKerStack[LOSCFG_BOXA_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosMuxCB      g_boxaMuxCB[LOSCFG_BOXA_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxaMuxEntity[LOSCFG_BOXA_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosQueueCB    g_boxaQueCB[LOSCFG_BOXA_QUEUE_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxaQueEntity[LOSCFG_BOXA_QUEUE_LIMIT];
LITE_OS_SEC_BOX_RESSPACE SIdObj        g_boxaSIdPool[LOSCFG_BOXA_SID_LIMIT];

#ifdef LOSCFG_SHELL
STATIC UINT32 OsShellCmdTest(UINT32 argc, const CHAR **argv)
{
    PRINTK("OsShellCmdTest: boxA\n");
    return 0;
}
#endif

VOID boxAEntry(VOID)
{
    /* Kernel side: box heap is initialized in OsBoxEnvBuild. */
    while (1) {
        LOS_PrintStr("Hello boxA!\n");
        UINT32 r = LOS_OffsetTest(1, 2, 3, 4, 5, 6, 7, 8);
        if (r == 0x12345678U) {
            LOS_PrintStr("OffsetTest 8-arg OK\n");
        } else {
            LOS_PrintStr("OffsetTest FAIL\n");
        }
        LOS_TaskDelay(1000);
    }
}

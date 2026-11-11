#include "stdio.h"
#include "boxes_conf.h"
#include "boxes_res_conf.h"
#include "los_memory.h"
#include "syscalls.h"

LOS_TASK_STACK_ALIGN UINT8 g_boxbStack[LOS_BOXB_STACK_SIZE]
    __attribute__((section(".boxB.bss"), used));

LITE_OS_SEC_BOX_RESSPACE LosTaskCB     g_boxbTaskCB[LOSCFG_BOXB_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosTaskStack  g_boxbKerStack[LOSCFG_BOXB_TASK_LIMIT];
LITE_OS_SEC_BOX_RESSPACE LosMuxCB      g_boxbMuxCB[LOSCFG_BOXB_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE EntityObj     g_boxbMuxEntity[LOSCFG_BOXB_MUX_LIMIT];
LITE_OS_SEC_BOX_RESSPACE SIdObj        g_boxbSIdPool[LOSCFG_BOXB_SID_LIMIT];

VOID boxBEntry(VOID)
{
    /* Kernel side: box heap is initialized in OsBoxEnvBuild. */
    while (1) {
        LOS_PrintStr("Hello boxB!\n");
        LOS_TaskDelay(500);
    }
}

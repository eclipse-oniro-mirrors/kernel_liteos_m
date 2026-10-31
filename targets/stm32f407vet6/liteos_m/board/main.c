/*
 * STM32F407VET6 LiteOS-M minimal demo:
 * one task printing "helloworld" endlessly over USART1 (115200).
 */

#include <stdio.h>
#include "los_config.h"
#include "los_task.h"
#include "uart.h"
#ifdef LOSCFG_BOX
#include "los_box_pri.h"
#include "boxes_conf.h"
#endif

static UINT32 g_helloworldTaskId;

static UINT32 HelloworldTask(UINT32 arg)
{
    (void)arg;
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

LITE_OS_SEC_TEXT_INIT int main(void)
{
    UINT32 ret;

    UartInit();

    ret = LOS_KernelInit();
    if (ret != LOS_OK) {
        printf("LiteOS kernel init failed! ERROR: 0x%x\n", ret);
        goto EXIT;
    }

#ifdef LOSCFG_BOX
    ret = OsBoxInit(g_userBoxCBArr, g_sysMemLayoutArr);
    if (ret != LOS_OK) {
        printf("OsBoxInit failed! ERROR: 0x%x\n", ret);
    }

    /* Static MPU regions (R0-R2) + box slots armed + SVC vector hook.
     * Kernel keeps running through the PRIVDEFENA background map. */
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

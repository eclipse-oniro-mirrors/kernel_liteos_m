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
#ifdef LOSCFG_BOX
    /* refs convention: box init runs AFTER the scheduler has started
     * (refs LOS_BoxInit is itself a syscall), so the first task ever
     * dispatched by the scheduler is always a KERNEL task - ArchStartToRun
     * (like refs OsStartToRun) has no user-task branch. The box entry
     * tasks created here enter user mode through the normal PendSV INUSER
     * path, exactly like every later user task. */
    UINT32 ret = OsBoxInit(g_userBoxCBArr, g_sysMemLayoutArr);
    if (ret != LOS_OK) {
        printf("OsBoxInit failed! ERROR: 0x%x\n", ret);
    }

    /* M5B-R regression: boxB's entry task exits on its own; re-create it
     * from the kernel. boxB's pools hold exactly ONE TCB + ONE pool
     * kernel-stack node, so every cycle strictly depends on the previous
     * exit returning both nodes - the pre-M5B-R bug leaked the
     * kernel-stack node (an usrStack==0 guard that never fired for box
     * tasks) and would fail the very first re-create with TSK_NO_MEMORY. */
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

    /* M5D lifecycle: destroy boxB (tear down tasks + reset SID pool) then
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

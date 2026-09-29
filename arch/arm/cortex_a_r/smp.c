/*
 * Copyright (c) 2024 morpheus
 */

#include "los_typedef.h"
#include "los_percpu_pri.h"
#include "los_arch_context.h"
#include "los_arch_interrupt.h"
#include "los_task_pri.h"
#include "los_sched_pri.h"
#include "los_swtmr_pri.h"

VOID OsMmuEnable(VOID);

volatile UINT32 g_secondaryOnline = 0;

VOID HalSecondaryCpuStart(VOID)
{
    OsCurrTaskSet(&g_osTaskCBArray[g_taskMaxNum]);

    OsMmuEnable();

    HalIrqInitPercpu();

    ArchTickTimerStartPerCpu();

    g_secondaryOnline = 1;
    __asm__ volatile("dsb" ::: "memory");

    (VOID)OsIdleTaskCreate();

    OsSchedStart();

    while (1) {
        __asm__ volatile("wfi");
    }
}

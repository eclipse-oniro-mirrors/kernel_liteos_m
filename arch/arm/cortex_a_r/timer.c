/*
 * Copyright (c) 2024 morpheus
 * Cortex-A7 ARM Generic Timer (CP15 c14)
 */

#include "los_timer.h"
#include "los_config.h"
#include "los_tick.h"
#include "los_arch_interrupt.h"
#include "los_interrupt.h"
#include "los_debug.h"
#include "los_typedef.h"

STATIC UINT32 GicTickStart(HWI_PROC_FUNC handler);
STATIC UINT64 GicTickReload(UINT64 nextResponseTime);
STATIC UINT64 GicTickCycleGet(UINT32 *period);
STATIC VOID GicTickLock(VOID);
STATIC VOID GicTickUnlock(VOID);

STATIC VOID HalTickEntry(VOID *parm);

STATIC ArchTickTimer g_archTickTimer = {
    .freq = 0,
    .irqNum = OS_GIC_TIMER_IRQ,
    .periodMax = LOSCFG_BASE_CORE_TICK_RESPONSE_MAX,
    .init = GicTickStart,
    .getCycle = GicTickCycleGet,
    .reload = GicTickReload,
    .lock = GicTickLock,
    .unlock = GicTickUnlock,
    .tickHandler = NULL,
};

/* CP15 c14 access helpers */
STATIC INLINE UINT32 GetCntFrq(VOID)
{
    UINT32 val;
    __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(val));
    return val;
}
STATIC INLINE VOID SetCntFrq(UINT32 val)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c0, 0" :: "r"(val));
    __asm__ volatile("isb" ::: "memory");
}
STATIC INLINE UINT64 GetCntpct(VOID)
{
    UINT32 low, high;
    __asm__ volatile("mrrc p15, 0, %0, %1, c14" : "=r"(low), "=r"(high));
    return ((UINT64)high << 32) | (UINT64)low;
}
STATIC INLINE UINT32 GetCntptval(VOID)
{
    UINT32 val;
    __asm__ volatile("mrc p15, 0, %0, c14, c2, 0" : "=r"(val));
    return val;
}
STATIC INLINE VOID SetCntptval(UINT32 val)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 0" :: "r"(val));
}

STATIC INLINE UINT64 GetCntpcval(VOID)
{
    UINT32 low, high;
    __asm__ volatile("mrrc p15, 2, %0, %1, c14" : "=r"(low), "=r"(high));
    return ((UINT64)high << 32) | (UINT64)low;
}
STATIC INLINE VOID SetCntpcval(UINT64 val)
{
    UINT32 low = (UINT32)val;
    UINT32 high = (UINT32)(val >> 32);
    __asm__ volatile("mcrr p15, 2, %0, %1, c14" :: "r"(low), "r"(high));
    __asm__ volatile("isb" ::: "memory");
}
STATIC INLINE UINT32 GetCntpCtl(VOID)
{
    UINT32 val;
    __asm__ volatile("mrc p15, 0, %0, c14, c2, 1" : "=r"(val));
    return val;
}
STATIC INLINE VOID SetCntpCtl(UINT32 val)
{
    __asm__ volatile("mcr p15, 0, %0, c14, c2, 1" :: "r"(val));
    __asm__ volatile("isb" ::: "memory");
}

STATIC UINT32 GicTickStart(HWI_PROC_FUNC handler)
{
    ArchTickTimer *tick = &g_archTickTimer;

    if (GetCntFrq() == 0) {
        SetCntFrq(OS_SYS_CLOCK);
    }
    tick->freq = OS_SYS_CLOCK;

    tick->tickHandler = handler;
#if (LOSCFG_PLATFORM_HWI_WITH_ARG == 1)
    OsSetVector((UINT32)tick->irqNum, HalTickEntry, NULL);
#else
    OsSetVector((UINT32)tick->irqNum, HalTickEntry);
#endif

    (VOID)LOS_HwiEnable((HWI_HANDLE_T)tick->irqNum);

    SetCntpCtl(0);
    GicTickReload((UINT64)(tick->freq / LOSCFG_BASE_CORE_TICK_PER_SECOND));

    return LOS_OK;
}

STATIC UINT64 GicTickReload(UINT64 nextResponseTime)
{
    if (nextResponseTime > g_archTickTimer.periodMax) {
        nextResponseTime = g_archTickTimer.periodMax;
    }
    if (nextResponseTime == 0) {
        nextResponseTime = 1;
    }

    SetCntpCtl(0x0);
    SetCntpcval(GetCntpct() + nextResponseTime);
    SetCntpCtl(0x1);
    return nextResponseTime;
}

STATIC VOID HalTickEntry(VOID *parm)
{
    (VOID)parm;
    OsTickHandler();
    UINT32 period = g_archTickTimer.freq / LOSCFG_BASE_CORE_TICK_PER_SECOND;
    SetCntpCtl(0x0);
    SetCntpcval(GetCntpct() + period);
    SetCntpCtl(0x1);
}

STATIC UINT64 GicTickCycleGet(UINT32 *period)
{
    UINT64 curr;
    UINT32 intSave = LOS_IntLock();
    curr = GetCntpct();
    if (period != NULL) {
        *period = GetCntptval();
    }
    LOS_IntRestore(intSave);
    return curr;
}

STATIC VOID GicTickLock(VOID)
{
    SetCntpCtl(GetCntpCtl() & ~0x1U);
}

STATIC VOID GicTickUnlock(VOID)
{
    SetCntpCtl(GetCntpCtl() | 0x1U);
}

ArchTickTimer *ArchSysTickTimerGet(VOID)
{
    return &g_archTickTimer;
}

UINT32 ArchEnterSleep(VOID)
{
    __asm__ volatile("dsb" ::: "memory");
    __asm__ volatile("wfi");
    __asm__ volatile("isb" ::: "memory");
    return LOS_OK;
}

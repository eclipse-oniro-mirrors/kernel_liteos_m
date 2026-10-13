/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * ArchTickTimer for AArch64 on RK3568: ARM generic EL1 physical timer
 * (CNTPCT/CNTP_CVAL/CNTP_CTL, PPI 30). Fully local: no master-side kick.
 *
 * Why PPI 30 needs a priority fix: the GIC programs it with priority 0xf0
 * by default while the slave's ICC_PMR_EL1 projects to 0xf0 as well
 * (PRIbits=5), so without the fix the tick stays pending-never-delivered.
 * PTimerTickStart rewrites the INTID 30 priority to 0x00 - non-secure
 * writes to IPRIORITYR do land (verified on board, same fix as the
 * rpmsg SGI 7 kick).
 *
 * The earlier theory that the PPI group registers (RAZ/WI for NS) made
 * PPI 30 undeliverable was wrong: a control experiment on the plain
 * maxcpus=3 image (CPU3 never touched by the Linux secondary bring-up
 * chain) showed PPI 30 delivered at exactly 100 Hz once the priority was
 * fixed. The SGI-9 master-kick implementation this replaces stayed in the
 * git history (see the previous version of this file).
 *
 * The timer is one-shot; the ISR re-arms CNTP_CVAL before running the
 * handler so the level signal drops.
 */

#include "los_timer.h"
#include "los_config.h"
#include "los_tick.h"
#include "los_arch_interrupt.h"
#include "los_arch_timer.h"
#include "los_interrupt.h"
#include "los_debug.h"
#include "los_typedef.h"

/* CNTP_CTL bits */
#define PTIMER_ENABLE                      0x1U
#define PTIMER_IMASK                       0x2U
#define PTIMER_ISTATUS                     0x4U

/* AArch64 system register accessors (STRINGIFY comes from los_config.h). */
#define AARCH64_SYSREG_READ(reg)                                  \
    ({                                                            \
        UINT64 _val;                                              \
        __asm__ volatile("mrs %0, " STRINGIFY(reg) : "=r"(_val)); \
        _val;                                                     \
    })

#define AARCH64_SYSREG_WRITE(reg, val)                             \
    do {                                                           \
        __asm__ volatile("msr " STRINGIFY(reg) ", %0" :: "r"((UINT64)(val))); \
        __asm__ volatile("isb" ::: "memory"); \
    } while (0)

STATIC UINT32 PTimerTickStart(HWI_PROC_FUNC handler);
STATIC UINT64 PTimerTickReload(UINT64 nextResponseTime);
STATIC UINT64 PTimerTickCycleGet(UINT32 *period);
STATIC VOID PTimerTickLock(VOID);
STATIC VOID PTimerTickUnlock(VOID);

#if (LOSCFG_PLATFORM_HWI_WITH_ARG == 1)
STATIC VOID HalTickEntry(VOID *parm);
#else
STATIC VOID HalTickEntry(VOID);
#endif

STATIC ArchTickTimer g_archTickTimer = {
    .freq = 0, /* read from CNTFRQ at init */
    .irqNum = OS_GIC_TIMER_IRQ, /* PPI 30, CNTP */
    .periodMax = LOSCFG_BASE_CORE_TICK_RESPONSE_MAX,
    .init = PTimerTickStart,
    .getCycle = PTimerTickCycleGet,
    .reload = PTimerTickReload,
    .lock = PTimerTickLock,
    .unlock = PTimerTickUnlock,
    .tickHandler = NULL,
};

STATIC INLINE UINT32 TickPeriodGet(VOID)
{
    return g_archTickTimer.freq / LOSCFG_BASE_CORE_TICK_PER_SECOND;
}

STATIC UINT32 PTimerTickStart(HWI_PROC_FUNC handler)
{
    ArchTickTimer *tick = &g_archTickTimer;

    tick->freq = (UINT32)AARCH64_SYSREG_READ(cntfrq_el0);
    if (tick->freq == 0) {
        tick->freq = OS_SYS_CLOCK;
    }

    tick->tickHandler = handler;
#if (LOSCFG_PLATFORM_HWI_WITH_ARG == 1)
    OsSetVector((UINT32)tick->irqNum, HalTickEntry, NULL);
#else
    OsSetVector((UINT32)tick->irqNum, HalTickEntry);
#endif

    /* PPI 30 priority: the GIC default 0xf0 equals the projected PMR and
     * would mask the tick; rewrite to 0x00 (NS writes to IPRIORITYR land). */
    {
        UINT64 mpidr;
        volatile UINT32 *prio;

        __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
        /* RK3568: all cores have Aff0=0, Aff1 = core number (mica irq.h).
         * IPRIORITYR7 covers INTID 28-31; INTID 30 is byte 2. */
        prio = (volatile UINT32 *)(0xFD460000UL +
              ((UINTPTR)((mpidr >> 8) & 0xFFU) * 0x20000UL + 0x10000UL + 0x400UL + 28UL));
        UINT32 v = *prio;
        v = (v & ~(0xFFU << 16)) | (0x00U << 16); /* INTID 30 byte -> 0x00 */
        *prio = v;
    }

    (VOID)ArchIntEnable((HWI_HANDLE_T)tick->irqNum);

    /* one-shot: arm the first expiry; the ISR re-arms every tick */
    AARCH64_SYSREG_WRITE(cntp_cval_el0,
                         AARCH64_SYSREG_READ(cntpct_el0) + TickPeriodGet());
    AARCH64_SYSREG_WRITE(cntp_ctl_el0, PTIMER_ENABLE);

    return LOS_OK;
}

STATIC UINT64 PTimerTickReload(UINT64 nextResponseTime)
{
    if (nextResponseTime > g_archTickTimer.periodMax) {
        nextResponseTime = g_archTickTimer.periodMax;
    }
    if (nextResponseTime == 0) {
        nextResponseTime = 1;
    }
    AARCH64_SYSREG_WRITE(cntp_cval_el0,
                         AARCH64_SYSREG_READ(cntpct_el0) + nextResponseTime);
    return nextResponseTime;
}

#if (LOSCFG_PLATFORM_HWI_WITH_ARG == 1)
STATIC VOID HalTickEntry(VOID *parm)
#else
STATIC VOID HalTickEntry(VOID)
#endif
{
    (VOID)parm;
    /* re-arm before running the handler so the level signal drops */
    AARCH64_SYSREG_WRITE(cntp_cval_el0,
                         AARCH64_SYSREG_READ(cntpct_el0) + TickPeriodGet());
    (VOID)OsTickHandler();
}

STATIC UINT64 PTimerTickCycleGet(UINT32 *period)
{
    UINT64 curr;
    UINT32 intSave = LOS_IntLock();
    curr = AARCH64_SYSREG_READ(cntpct_el0);
    if (period != NULL) {
        /* "remaining time to the next expiry": ~0 by the time a tick ISR
         * samples it; reporting the full period would make
         * OsUpdateSysTimeBase inflate the time base once per tick. */
        *period = 0;
    }
    LOS_IntRestore(intSave);
    return curr;
}

STATIC VOID PTimerTickLock(VOID)
{
    AARCH64_SYSREG_WRITE(cntp_ctl_el0,
        (UINT32)AARCH64_SYSREG_READ(cntp_ctl_el0) | PTIMER_IMASK);
}

STATIC VOID PTimerTickUnlock(VOID)
{
    AARCH64_SYSREG_WRITE(cntp_ctl_el0,
        (UINT32)AARCH64_SYSREG_READ(cntp_ctl_el0) & ~((UINT32)PTIMER_IMASK));
}

ArchTickTimer *ArchSysTickTimerGet(VOID)
{
    return &g_archTickTimer;
}

UINT32 ArchEnterSleep(VOID)
{
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("wfi");
    __asm__ volatile("isb" ::: "memory");
    return LOS_OK;
}

/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * ArchTickTimer for AArch64 AMP slaves (RK3568 / RK3588): ARM generic
 * timer, fully local (no master-side kick). Two backends selected by
 * LOSCFG_ARCH_TIMER_VIRT (targets/<board>/liteos_m/board/target_config.h):
 *
 *   0 (default, RK3568): EL1 PHYSICAL timer (CNTPCT/CNTP_CVAL/CNTP_CTL,
 *      PPI 30) + WFI idle. The RK3568 boot chain leaves the physical-timer
 *      gates (CNTHCTL_EL2.EL1PCEN/EL1PCTEN) open, so CNTP accesses work at
 *      EL1 and the tick arrives as a normal interrupt once the GICR
 *      priority is fixed below.
 *
 *   1 (RK3588): EL1 VIRTUAL timer (CNTVCT/CNTV_CVAL/CNTV_CTL, PPI 27) +
 *      polling tick pump. The RK3588 slave is handed to NS EL1 directly by
 *      the OP-TEE/TF-A chain, so arm64_elX_to_el1() in los_el_switch.S
 *      never runs and CNTHCTL_EL2.EL1PCEN/EL1PCTEN stay 0: every CNTP
 *      access (CNTP_CTL/CVAL/TVAL, CNTPCT) from EL1 then traps to a dead
 *      EL2 (no VBAR_EL2) and the core locks up silently. The virtual
 *      counter/timer (CNTVCT, CNTV_*) is NOT gated by CNTHCTL and is
 *      always accessible at EL1. OP-TEE also leaves the CPU7 PPI lines in
 *      Group0 (with GICD_CTLR.DS == 0 the GICR group/priority registers
 *      are secure-only), so the PPI 27 interrupt is routed to EL3 and
 *      never reaches NS EL1 - SlaveTickPump() in ArchEnterSleep() delivers
 *      the tick by polling instead of WFI (see its comment).
 *
 * Both backends rewrite the timer PPI priority in the redistributor: the
 * GIC default 0xf0 equals the projected ICC_PMR_EL1 (PRIbits=5) and would
 * mask the tick; rewriting to 0x00 fixes it (non-secure writes to
 * IPRIORITYR land on these boards - same fix as the rpmsg SGI 7 kick).
 *
 * The timer is one-shot; the ISR (or the pump) re-arms the comparator
 * before running the handler so the level signal drops.
 */

#include "los_timer.h"
#include "los_config.h"
#include "los_tick.h"
#include "los_arch_interrupt.h"
#include "los_arch_timer.h"
#include "los_interrupt.h"
#include "los_debug.h"
#include "los_typedef.h"

/* timer CTL bits (identical layout for CNTP/CNTV) */
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

/* Backend selection - see the file header. Default: physical timer. */
#ifndef LOSCFG_ARCH_TIMER_VIRT
#define LOSCFG_ARCH_TIMER_VIRT             0
#endif

#if (LOSCFG_ARCH_TIMER_VIRT == 1)
#define VIRT_TIMER_PPI                     27U    /* EL1 virtual timer line */
#define TICK_IRQ_NUM                       VIRT_TIMER_PPI
#define TICK_CVAL_READ()                   AARCH64_SYSREG_READ(cntv_cval_el0)
#define TICK_CVAL_WRITE(val)               AARCH64_SYSREG_WRITE(cntv_cval_el0, (val))
#define TICK_CTL_READ()                    ((UINT32)AARCH64_SYSREG_READ(cntv_ctl_el0))
#define TICK_CTL_WRITE(val)                AARCH64_SYSREG_WRITE(cntv_ctl_el0, (val))
#define TICK_CT_READ()                     AARCH64_SYSREG_READ(cntvct_el0)
#else
#define TICK_IRQ_NUM                       OS_GIC_TIMER_IRQ /* PPI 30, CNTP */
#define TICK_CVAL_READ()                   AARCH64_SYSREG_READ(cntp_cval_el0)
#define TICK_CVAL_WRITE(val)               AARCH64_SYSREG_WRITE(cntp_cval_el0, (val))
#define TICK_CTL_READ()                    ((UINT32)AARCH64_SYSREG_READ(cntp_ctl_el0))
#define TICK_CTL_WRITE(val)                AARCH64_SYSREG_WRITE(cntp_ctl_el0, (val))
#define TICK_CT_READ()                     AARCH64_SYSREG_READ(cntpct_el0)
#endif

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
    .irqNum = TICK_IRQ_NUM,
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

#if (LOSCFG_ARCH_TIMER_VIRT == 1)
    /* clear any stale virtual-timer state the previous world left on this
     * CPU (enabled/pending), so enabling PPI 27 cannot latch a bogus tick */
    AARCH64_SYSREG_WRITE(cntv_ctl_el0, 0);

    /* PPI 27 GICR setup. The enable write below lands (verified: bit 27
     * reads back set in ISENABLER0). The group/priority writes however
     * do NOT: with GICD_CTLR.DS == 0 they are secure-only, and OP-TEE's
     * stay on CPU7 left PPI 27 in Group0 - its FIQ is swallowed at EL3
     * and the tick never fires as an interrupt. The writes are kept for
     * diagnosis; the actual tick delivery is the SlaveTickPump() polling
     * path in ArchEnterSleep(). */
    {
        INT32 slot = GicrSlotProbe();
        volatile UINT32 *igroupr;
        volatile UINT32 *prio;
        UINT32 v;

        if (slot < 0) {
            /* No TYPER match (unexpected): fall back to the single-core
             * cluster layout - core number in MPIDR Aff1. */
            UINT64 mpidr;
            __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
            slot = (INT32)((mpidr >> 8) & 0xFFU);
        }
        /* SGI/PPI frame of our rdist slot: base + slot*0x20000 + 0x10000 */
        igroupr = (volatile UINT32 *)(GICR_BASE +
                  ((UINTPTR)slot * 0x20000UL + 0x10000UL + 0x80UL));
        /* IPRIORITYR6 covers INTID 24-27; INTID 27 is byte 3. */
        prio = (volatile UINT32 *)(GICR_BASE +
              ((UINTPTR)slot * 0x20000UL + 0x10000UL + 0x400UL + 24UL));
        v = *igroupr;
        if (((v >> 27) & 1U) == 0) {
            *igroupr = v | (1U << 27);   /* PPI 27 -> Group1 non-secure */
        }

        v = *prio;
        v = (v & ~(0xFFU << 24)) | (0x00U << 24); /* INTID 27 byte -> 0x00 */
        *prio = v;
    }
#else
    /* PPI 30 priority: the GIC default 0xf0 equals the projected PMR and
     * would mask the tick; rewrite to 0x00 (NS writes to IPRIORITYR land). */
    {
        UINT64 mpidr;
        volatile UINT32 *prio;

        __asm__ volatile("mrs %0, mpidr_el1" : "=r"(mpidr));
        /* RK3568: all cores have Aff0=0, Aff1 = core number (mica irq.h).
         * IPRIORITYR7 covers INTID 28-31; INTID 30 is byte 2. GICR_BASE is
         * the RK3568 redistributor base in the board soc.h. */
        prio = (volatile UINT32 *)(GICR_BASE +
              ((UINTPTR)((mpidr >> 8) & 0xFFU) * 0x20000UL + 0x10000UL + 0x400UL + 28UL));
        UINT32 v = *prio;
        v = (v & ~(0xFFU << 16)) | (0x00U << 16); /* INTID 30 byte -> 0x00 */
        *prio = v;
    }
#endif

    (VOID)ArchIntEnable((HWI_HANDLE_T)tick->irqNum);

    /* one-shot: arm the first expiry; the ISR (or the pump below) re-arms
     * every tick. For the virtual backend the counter read also proves the
     * path is not trapped (CNTHCTL gates only the physical counter/timer). */
    TICK_CVAL_WRITE(TICK_CT_READ() + TickPeriodGet());
    TICK_CTL_WRITE(PTIMER_ENABLE);

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
    TICK_CVAL_WRITE(TICK_CT_READ() + nextResponseTime);
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
    TICK_CVAL_WRITE(TICK_CT_READ() + TickPeriodGet());
    (VOID)OsTickHandler();
}

STATIC UINT64 PTimerTickCycleGet(UINT32 *period)
{
    UINT64 curr;
    UINT32 intSave = LOS_IntLock();
    curr = TICK_CT_READ();
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
    TICK_CTL_WRITE(TICK_CTL_READ() | PTIMER_IMASK);
}

STATIC VOID PTimerTickUnlock(VOID)
{
    TICK_CTL_WRITE(TICK_CTL_READ() & ~((UINT32)PTIMER_IMASK));
}

ArchTickTimer *ArchSysTickTimerGet(VOID)
{
    return &g_archTickTimer;
}

#if (LOSCFG_ARCH_TIMER_VIRT == 1)
/*
 * RK3588 OP-TEE residue (measured on board): with GICD_CTLR.DS == 0 the
 * GICR SGI/PPI group and priority registers of CPU7 are only writable
 * from secure state. PPI 27 (the EL1 virtual timer this port uses) is
 * stuck in Group0 - its FIQ is routed to EL3 by OP-TEE's SCR_EL3 and
 * never reaches NS EL1. Evidence: the NS write of priority 0x00 to
 * GICR_IPRIORITYR6 does not land (reads back 0xf0f0f0f0) while the same
 * register on the Linux cores accepts NS writes (0xa0a0a0a0), and the
 * enabled PPI 27 sits pending forever without ever being acknowledged.
 *
 * Fix: pump the tick by polling. The idle task calls ArchEnterSleep()
 * every loop; when CNTVCT has passed CNTV_CVAL the tick handler runs
 * inline (task context with interrupts locked - identical semantics to
 * the swalled ISR). All board demo tasks block on LOS_TaskDelay or
 * semaphores, so the idle task is guaranteed to run between ticks.
 */
STATIC VOID SlaveTickPump(VOID)
{
    UINT64 now = TICK_CT_READ();
    UINT64 cval = TICK_CVAL_READ();
    UINT32 intSave;

    if ((INT64)(now - cval) < 0) {
        return; /* deadline not reached yet */
    }
    intSave = LOS_IntLock();
#if (LOSCFG_PLATFORM_HWI_WITH_ARG == 1)
    HalTickEntry(NULL);
#else
    HalTickEntry();
#endif
    LOS_IntRestore(intSave);
}
#endif /* LOSCFG_ARCH_TIMER_VIRT */

UINT32 ArchEnterSleep(VOID)
{
#if (LOSCFG_ARCH_TIMER_VIRT == 1)
    /* No WFI: every interrupt line this slave could receive (PPI 27
     * virtual timer, SGI 7 mcs kick - both pending in the redistributor
     * right now) is stuck in Group0 and routed to EL3 by OP-TEE, so for
     * NS EL1 they are "masked" and generate no WFI wake event. Sleeping
     * here would park the core forever after a single pump pass. The
     * idle loop spins instead: each OsIdleTask iteration calls this
     * function, giving a microsecond-scale poll cadence - CNTVCT reads
     * are register-local, so the bus is not hammered. The cost is 100%
     * CPU7 utilization while idle, acceptable for an AMP demo core. */
    SlaveTickPump();
#else
    __asm__ volatile("dsb sy" ::: "memory");
    __asm__ volatile("wfi");
    __asm__ volatile("isb" ::: "memory");
#endif
    return LOS_OK;
}

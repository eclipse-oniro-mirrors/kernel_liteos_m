/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "los_timer.h"
#include "los_config.h"
#include "los_tick.h"
#include "los_interrupt.h"
#include "los_arch_interrupt.h"
#include "los_hwi_pri.h"
#include "los_debug.h"
#include "los_typedef.h"
#include "asm/register_config.h"
#include "asm/interrupt_config.h"

#ifndef DW_APB_TIMER_BASE
#error "DW_APB_TIMER_BASE must be defined by the board (asm/register_config.h or soc.h)"
#endif

#ifndef DW_APB_TIMER_IRQ
#ifdef NUM_HAL_INTERRUPT_TIMER
#define DW_APB_TIMER_IRQ           NUM_HAL_INTERRUPT_TIMER
#else
#error "DW_APB_TIMER_IRQ or NUM_HAL_INTERRUPT_TIMER must be defined by the board"
#endif
#endif

#ifndef DW_APB_TIMER_CLOCK
#ifdef TIMER_CLOCK
#define DW_APB_TIMER_CLOCK         TIMER_CLOCK
#else
#define DW_APB_TIMER_CLOCK         OS_SYS_CLOCK
#endif
#endif

#ifndef DW_APB_TIMER_IRQ_PRIO
#define DW_APB_TIMER_IRQ_PRIO      2
#endif

#ifndef DW_APB_TIMER_UNLOCK_VAL
#define DW_APB_TIMER_UNLOCK_VAL    0x5A5A5A5Au
#endif

/* DW APB timer register offsets (standard layout). */
#define DW_APB_TIMER_LOAD0         0x00
#define DW_APB_TIMER_LOAD1         0x04
#define DW_APB_TIMER_CURRENT       0x08
#define DW_APB_TIMER_CONTROL       0x10
#define DW_APB_TIMER_EOI           0x14

/* Control register: 0x0 = disabled, 0x3 = enable + user-defined mode
 * (periodic, reloads from LOAD). */
#define DW_APB_TIMER_CTRL_DISABLE  0x0u
#define DW_APB_TIMER_CTRL_PERIODIC 0x3u

#define DW_APB_TIMER_TICK_RELOAD   (DW_APB_TIMER_CLOCK / LOSCFG_BASE_CORE_TICK_PER_SECOND)

#define DW_REG_READ(off)           (*(volatile UINT32 *)(UINTPTR)(DW_APB_TIMER_BASE + (off)))
#define DW_REG_WRITE(off, val)     (*(volatile UINT32 *)(UINTPTR)(DW_APB_TIMER_BASE + (off)) = (val))

/* Board interrupt gate (handles local mie bits vs external controller). */
extern VOID HalIrqEnable(UINT32 vector);
extern VOID HalIrqDisable(UINT32 vector);

STATIC HWI_PROC_FUNC g_sysTickHandler = (HWI_PROC_FUNC)NULL;

STATIC VOID DwApbTickIrqHandler(VOID *arg)
{
    (VOID)arg;

    /* EOI re-arms the raw-interrupt (edge source) first; the pending bit is
     * already cleared by HalHwiInterruptDone. Do NOT clear it again here:
     * an expiry landing between the two would stick raw_intr high and the
     * tick line would never edge again (field forensics). */
    DW_REG_WRITE(DW_APB_TIMER_EOI, 1);
#ifdef DW_APB_TIMER_EOI_FENCE
    __asm__ __volatile__("fence" ::: "memory");
#endif

    if (g_sysTickHandler != NULL) {
        g_sysTickHandler(NULL);
    }
}

STATIC UINT32 DwApbTickStart(HWI_PROC_FUNC handler)
{
    HwiIrqParam irqParam = {
        .swIrq = DW_APB_TIMER_IRQ,
        .pDevId = NULL,
        .pName = "dw_apb_timer",
    };

    g_sysTickHandler = handler;

    UINT32 ret = LOS_HwiCreate(DW_APB_TIMER_IRQ, DW_APB_TIMER_IRQ_PRIO, 0, DwApbTickIrqHandler, &irqParam);
    if (ret != LOS_OK) {
        PRINT_ERR("DW APB timer HwiCreate failed: 0x%x\n", ret);
        return ret;
    }

#ifdef DW_APB_TIMER_NEED_UNLOCK
    *(volatile UINT32 *)(UINTPTR)DW_APB_TIMER_LOCK_BASE = DW_APB_TIMER_UNLOCK_VAL;
#endif
    DW_REG_WRITE(DW_APB_TIMER_CONTROL, DW_APB_TIMER_CTRL_DISABLE);
    DW_REG_WRITE(DW_APB_TIMER_LOAD0, DW_APB_TIMER_TICK_RELOAD);
    /* LOAD_COUNT1 is the high word of the 64-bit cascade (LOAD1:LOAD0);
     * the reload fits 32 bits on every current board, so high word = 0. */
    DW_REG_WRITE(DW_APB_TIMER_LOAD1, 0);
    DW_REG_WRITE(DW_APB_TIMER_EOI, 1);
#ifdef DW_APB_TIMER_EOI_FENCE
    __asm__ __volatile__("fence" ::: "memory");
#endif

    HalIrqEnable(DW_APB_TIMER_IRQ);
    DW_REG_WRITE(DW_APB_TIMER_CONTROL, DW_APB_TIMER_CTRL_PERIODIC);

    /*
     * MUST stay PERIODIC, never one-shot: the first window fires during
     * OsSchedStart, before mret/MIE — a one-shot would expire and stop,
     * silently losing the interrupt. (Carried over from the vendor timers.)
     */
    return LOS_OK;
}

STATIC UINT64 DwApbGetCycle(UINT32 *period)
{
    if (period != NULL) {
        *period = DW_APB_TIMER_TICK_RELOAD;
    }
#ifdef DW_APB_TIMER_CURRENT_BROKEN
    /* Bare CURRENT_VALUE reads return 0 on this SoC (the v150 IP needs a
     * cnt_req/cnt_lock capture handshake first — CONTROL bits 5/6). A
     * bogus read makes the kernel's rollover compensation advance the
     * base by a full period (phantom ticks). The tick ISR rate is
     * hardware-exact, so report a constant zero offset and let the base
     * advance only from the ISR. Also a blocker for tickless (see
     * DwApbReload). */
    return 0;
#else
    return (UINT64)(DW_APB_TIMER_TICK_RELOAD - DW_REG_READ(DW_APB_TIMER_CURRENT));
#endif
}

#ifdef DW_APB_TIMER_REARM_ON_FIRST_RELOAD
/* Set when the tick has been (re)armed with MIE about to go live. */
STATIC volatile BOOL g_dwApbTickArmed = FALSE;
#endif

STATIC UINT64 DwApbReload(UINT64 nextResponseTime)
{
    /* No-op reload: periodic mode only. KNOWN LIMITATION: with tickless
     * enabled the kernel's reprogramming is ignored — tickless degrades
     * to plain periodic ticking. NOT verified under an actual tickless
     * build; proper support needs one-shot reload AND a working getCycle
     * (see CURRENT_BROKEN) together. */
    (VOID)nextResponseTime;
#ifdef DW_APB_TIMER_REARM_ON_FIRST_RELOAD
    /* CLIC quirk: a pending latched while mstatus.MIE=0 never arbitrates
     * once MIE=1 (TIMER2 probe, same config but first edge after MIE=1,
     * delivered 20/20). The tick starts at SYS_INIT_LEVEL_ARCH, long
     * before the first mret, so its first expiry lands in that window:
     * raw level stuck, ip dead-latched. The FIRST reload call comes from
     * OsSchedStart microseconds before mret — flush and restart here so
     * the first edge lands with MIE=1. */
    if (!g_dwApbTickArmed) {
        HwiControllerOps *ops = HwiControllerOpsGet();
        g_dwApbTickArmed = TRUE;
        DW_REG_WRITE(DW_APB_TIMER_CONTROL, DW_APB_TIMER_CTRL_DISABLE);
        DW_REG_WRITE(DW_APB_TIMER_EOI, 1);   /* drop the asserted raw level */
#ifdef DW_APB_TIMER_EOI_FENCE
        __asm__ __volatile__("fence" ::: "memory");
#endif
        if ((ops != NULL) && (ops->clearIrq != NULL)) {
            (VOID)ops->clearIrq(DW_APB_TIMER_IRQ);  /* drop the dead latched ip */
        }
        DW_REG_WRITE(DW_APB_TIMER_CONTROL, DW_APB_TIMER_CTRL_PERIODIC);
    }
#endif
    return nextResponseTime;
}

STATIC VOID DwApbLock(VOID)
{
    HalIrqDisable(DW_APB_TIMER_IRQ);
}

STATIC VOID DwApbUnlock(VOID)
{
    HalIrqEnable(DW_APB_TIMER_IRQ);
}

STATIC ArchTickTimer g_dwApbTickTimer = {
    .freq = DW_APB_TIMER_CLOCK,
    .irqNum = DW_APB_TIMER_IRQ,
    .periodMax = LOSCFG_BASE_CORE_TICK_RESPONSE_MAX,
    .init = DwApbTickStart,
    .getCycle = DwApbGetCycle,
    .reload = DwApbReload,
    .lock = DwApbLock,
    .unlock = DwApbUnlock,
    .tickHandler = NULL,
};

/* Sole provider on DW-APB boards; weak for a future board override. */
__attribute__((weak)) ArchTickTimer *ArchSysTickTimerGet(VOID)
{
    return &g_dwApbTickTimer;
}

#ifdef LOSCFG_ARCH_RISCV32
/* Idle low-power entry (WFI), kept beside the tick per kernel convention
 * (every arch los_timer.c defines it alongside). Weak: a board wanting a
 * deeper idle overrides it. */
__attribute__((weak)) UINT32 ArchEnterSleep(VOID)
{
    wfi();
    return LOS_OK;
}
#endif

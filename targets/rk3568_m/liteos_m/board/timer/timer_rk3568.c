/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * timer_rk3568.c - irq-mode SoC timer driver for the RK3568 AMP
 * slave core (TIMER1 fe5f0020, 24MHz, contract: timer.h). TIMER0 is
 * the master clockevent (dtb timer@fe5f0000), so the slave binds
 * TIMER1 - a channel the master never touches - and self-serves the
 * CRU clock gate + channel reset (no ko precondition, and the
 * block-wide SRST_P_TIMER is never pulsed).
 * register layout (rk3288-compatible): LOAD_COUNT0/1, CURRENT_VALUE0/1,
 * CONTROL_REG @ 0x10 (bit0 enable, bit1 mode: 1 = user-defined - the
 * counter starts from LOAD_COUNT on enable, latches ONE interrupt at
 * zero and stops there - there is no hardware auto-reload / 0 =
 * free-running - counts from 0xffffffff and ignores LOAD; bit2 int
 * unmask), INT_STATUS @ 0x18 (write-1-to-clear).
 * instances: g_ports[] carries the per-channel profile (timer0_profile.h);
 * all runtime state lives in the per-port timerDevice, so channels run
 * concurrently with no shared mutable state.
 * both work modes use user-defined single-shot: a periodic channel is
 * maintained by the driver - the ISR rewrites LOAD and re-enables the
 * channel on every expiry (the same reprogram flow as linux
 * timer-rockchip.c set_next_event); oneshot just lets the channel stop
 * at zero.
 * no DMA, no dynamic allocation, no master handshake: direct MMIO.
 * minimal set: interrupt-driven expiry only (irq-only contract).
 */

#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "los_arch_interrupt.h"
#include "timer.h"
#include "timer0_profile.h"

/* ------------------------------------------------------------------ */
/* register map (offsets relative to the channel base)                  */
/* ------------------------------------------------------------------ */

#define TIMER_LOAD_COUNT0       0x00u
#define TIMER_LOAD_COUNT1       0x04u
#define TIMER_CONTROL_REG       0x10u
#define TIMER_INT_STATUS        0x18u

#define TIMER_CTRL_ENABLE       (1u << 0)
#define TIMER_CTRL_USER_DEF     (1u << 1)  /* 0 = free-running reload */
#define TIMER_CTRL_INT_UNMASK   (1u << 2)

/* ------------------------------------------------------------------ */
/* LOS interrupt API (same extern contract as can/net/uart)             */
/* ------------------------------------------------------------------ */

extern UINT32 ArchHwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio,
                            HWI_MODE_T hwiMode, HWI_PROC_FUNC hwiHandler,
                            HwiIrqParam *irqParam);
extern UINT32 ArchHwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam);

/* ------------------------------------------------------------------ */
/* port profile + device                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uintptr_t base;
    uint32_t clockHz;
    uint32_t intid;
    uint32_t irqPrio;
    uintptr_t cruGateReg;
    uint32_t cruGateBits;
    uintptr_t cruRstReg;
    uint32_t cruRstBits;
} timerPortProfile;

typedef enum {
    TIMER_STATE_UNUSED = 0,
    TIMER_STATE_READY,
    TIMER_STATE_RUNNING,
    TIMER_STATE_FAULT,
} timerState;

struct timerDevice {
    const timerPortProfile *port;
    uint32_t portId;
    uint32_t mode;          /* TIMER_MODE_* */
    uint64_t loadTicks;     /* LOAD_COUNT value: period = loadTicks + 1 */
    volatile uint32_t running;
    timerCallback callback;
    void *callbackArg;
    timerState state;
};

static const timerPortProfile g_ports[] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 }, /* portId 0 unused */
    { /* TIMER_PORT_TIMER1_RK3568 == 1 */
        TIMER1_BASE, TIMER1_CLOCK_HZ, TIMER1_IRQ_INTID, TIMER1_IRQ_LOS_PRIO,
        TIMER1_CRU_GATE_REG, TIMER1_CRU_GATE_BITS,
        TIMER1_CRU_RST_REG, TIMER1_CRU_RST_BITS,
    },
};

#define TIMER_PORT_COUNT (sizeof(g_ports) / sizeof(g_ports[0]))

static timerDevice g_devices[TIMER_PORT_COUNT];

/* ------------------------------------------------------------------ */
/* MMIO helpers                                                         */
/* ------------------------------------------------------------------ */

static inline void TimerRegWrite(const timerDevice *d, uint32_t off,
                                 uint32_t val)
{
    *(volatile uint32_t *)(d->port->base + off) = val;
}

static inline void TimerMmioBarrier(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

/* CRU helpers: writes use the write-mask convention - the high half
 * word selects which low-half bits actually change, so a driver only
 * ever toggles the bits it owns (bits = (sel << 16) | value). */
static inline uint32_t CruRead(uintptr_t reg)
{
    return *(volatile uint32_t *)reg;
}

static inline void CruWrite(uintptr_t reg, uint32_t val)
{
    *(volatile uint32_t *)reg = val;
}

/* ------------------------------------------------------------------ */
/* ISR                                                                  */
/* ------------------------------------------------------------------ */

static void TimerProgramLoad(const timerDevice *d, uint64_t ticks);
static void TimerChannelEnable(const timerDevice *d);

/* restart one period: the user-defined channel stopped at zero, and
 * the hardware has no auto-reload. only an ENABLE 0->1 EDGE transfers
 * LOAD into the counter - rewriting LOAD and re-asserting the same
 * ctrl value leaves the channel parked (proven on-target: a periodic
 * phase then fires exactly once). the stop-load-enable sequence below
 * is the one verified by the bring-up probe and mirrors linux
 * timer-rockchip.c set_next_event, which re-enables per event. */
static void TimerChannelRearm(const timerDevice *d)
{
    TimerRegWrite(d, TIMER_CONTROL_REG, 0u);
    TimerProgramLoad(d, d->loadTicks);
    TimerChannelEnable(d);
}

static VOID Timer0Isr(VOID)
{
    /* ArchHwiCreate takes a plain VOID(VOID) entry, so the single
     * channel today is bound directly; more channels get a per-port
     * isr table (see the can driver). */
    timerDevice *d = &g_devices[1]; /* TIMER_PORT_TIMER1_RK3568 */

    if (!d->running) {
        /* stray interrupt after a stop race: ack and leave */
        TimerRegWrite(d, TIMER_INT_STATUS, 1u);
        return;
    }
    /* ack first: the line must deassert before the GIC eoi */
    TimerRegWrite(d, TIMER_INT_STATUS, 1u);
    if (d->mode == TIMER_MODE_ONESHOT) {
        /* the channel already stopped at zero: it stays stopped */
        TimerRegWrite(d, TIMER_CONTROL_REG, 0u);
        d->running = 0;
    } else {
        /* no hardware auto-reload: start the next period now */
        TimerChannelRearm(d);
    }
    if (d->callback != NULL) {
        d->callback(d->callbackArg);
    }
}

/* ------------------------------------------------------------------ */
/* core programming                                                     */
/* ------------------------------------------------------------------ */

static void TimerProgramLoad(const timerDevice *d, uint64_t ticks)
{
    /* period = LOAD + 1 ticks; keep the high word for completeness
     * (a 32-bit LOAD at 24MHz already spans ~179s). */
    TimerRegWrite(d, TIMER_LOAD_COUNT0, (uint32_t)(ticks & 0xffffffffu));
    TimerRegWrite(d, TIMER_LOAD_COUNT1, (uint32_t)(ticks >> 32));
}

static void TimerChannelEnable(const timerDevice *d)
{
    uint32_t ctrl = TIMER_CTRL_ENABLE | TIMER_CTRL_USER_DEF;

    /* user-defined (bit1=1) is the only mode that uses LOAD_COUNT: the
     * counter starts from LOAD on enable, latches one interrupt at zero
     * and stops there - the channel does NOT auto-reload (verified
     * on-target: a periodic channel fires exactly once without driver
     * rearm; linux timer-rockchip.c re-enables per event for the same
     * reason). free-running (bit1=0) counts from 0xffffffff and never
     * uses LOAD, so no driver mode wants it. periodic is maintained by
     * TimerChannelRearm() on every ISR expiry, oneshot just stops at
     * zero. INT_UNMASK is set unconditionally (irq-only contract). */
    ctrl |= TIMER_CTRL_INT_UNMASK;
    TimerRegWrite(d, TIMER_CONTROL_REG, ctrl);
}

/* ------------------------------------------------------------------ */
/* public API                                                           */
/* ------------------------------------------------------------------ */

int TimerInit(const timerConfig *cfg, timerDevice **dev)
{
    timerDevice *d;
    const timerPortProfile *port;

    if ((cfg == NULL) || (dev == NULL)) {
        return TIMER_EINVAL;
    }
    if ((cfg->portId == 0) || (cfg->portId >= TIMER_PORT_COUNT)) {
        return TIMER_ENODEV;
    }
    d = &g_devices[cfg->portId];
    if (d->state == TIMER_STATE_RUNNING) {
        return TIMER_EBUSY;
    }
    port = &g_ports[cfg->portId];
    /* interval bounds: at least 2 counter ticks so a period exists,
     * and no more than the 64-bit LOAD span at the channel clock. */
    {
        uint64_t maxUs = 0xffffffffffffffffULL / (uint64_t)port->clockHz;

        if ((cfg->intervalUs == 0) ||
            (cfg->intervalUs * port->clockHz / 1000000ULL < 2) ||
            (cfg->intervalUs > maxUs)) {
            return TIMER_EINVAL;
        }
    }

    /* bind the port BEFORE any TimerRegWrite: the MMIO helpers reach the
     * registers through d->port->base, and g_devices starts zeroed - an
     * early write here dereferenced NULL and data-aborted the core. */
    d->port = port;

    /* CRU: un-gate PCLK_TIMER + the channel clock, then pulse the
     * channel reset (assert then release). the write-mask convention
     * uses the high half word to strobe the low half, and the mask
     * only selects OUR bits so other lines sharing the same CON
     * register are never touched. semantics: CLKGATE bit=1 gates the
     * clock off (write 0 to run it); SOFTRST bit=1 asserts the reset
     * (write 0 to release it). */
    CruWrite(port->cruGateReg, port->cruGateBits << 16);                  /* run */
    CruWrite(port->cruRstReg, (port->cruRstBits << 16) | port->cruRstBits); /* assert */
    CruWrite(port->cruRstReg, port->cruRstBits << 16);                    /* release */

    /* channel quiesced: disable + ack anything latched */
    TimerRegWrite(d, TIMER_CONTROL_REG, 0u);
    TimerRegWrite(d, TIMER_INT_STATUS, 1u);
    TimerMmioBarrier();

    d->portId = cfg->portId;
    /* mode 0 (ONESHOT) is a legal explicit value, not a "unset"
     * default - a zero here must configure a one-shot, so only values
     * outside the enum are refused */
    if (cfg->mode > TIMER_MODE_PERIODIC) {
        return TIMER_EINVAL;
    }
    d->mode = cfg->mode;
    d->callback = cfg->callback;
    d->callbackArg = cfg->callbackArg;
    d->running = 0;
    d->loadTicks = cfg->intervalUs * (uint64_t)port->clockHz / 1000000ULL - 1ULL;
    TimerProgramLoad(d, d->loadTicks);
    d->state = TIMER_STATE_READY;

    *dev = d;
    return 0;
}

int TimerStart(timerDevice *dev)
{
    if ((dev == NULL) || (dev->port == NULL)) {
        return TIMER_EINVAL;
    }
    if (dev->state == TIMER_STATE_RUNNING) {
        return 0;                       /* idempotent */
    }
    if (dev->state != TIMER_STATE_READY) {
        return TIMER_EIO;               /* FAULT: needs init first */
    }

    dev->running = 1;

    if (ArchHwiCreate((HWI_HANDLE_T)dev->port->intid,
                      (HWI_PRIOR_T)dev->port->irqPrio,
                      HWI_MODE_TRIGGER_LEVEL,
                      Timer0Isr, (HwiIrqParam *)0) != LOS_OK) {
        PRINT_ERR("[timer] ArchHwiCreate(%u) failed\n",
                  dev->port->intid);
        dev->running = 0;
        return TIMER_EIO;
    }

    /* fresh load so the first period starts now, then enable */
    TimerProgramLoad(dev, dev->loadTicks);
    TimerChannelEnable(dev);
    TimerMmioBarrier();
    dev->state = TIMER_STATE_RUNNING;
    return 0;
}

int TimerStop(timerDevice *dev)
{
    if ((dev == NULL) || (dev->port == NULL)) {
        return TIMER_EINVAL;
    }
    if (dev->state != TIMER_STATE_RUNNING) {
        return 0;                       /* idempotent */
    }

    dev->running = 0;
    TimerRegWrite(dev, TIMER_CONTROL_REG, 0u);
    TimerRegWrite(dev, TIMER_INT_STATUS, 1u);
    TimerMmioBarrier();
    (VOID)ArchHwiDelete((HWI_HANDLE_T)dev->port->intid,
                        (HwiIrqParam *)0);
    dev->state = TIMER_STATE_READY;
    return 0;
}

int TimerDeinit(timerDevice *dev)
{
    int ret;

    if ((dev == NULL) || (dev->port == NULL)) {
        return TIMER_EINVAL;
    }
    ret = TimerStop(dev);
    if (ret != 0) {
        return ret;
    }
    dev->port = NULL;
    dev->state = TIMER_STATE_UNUSED;
    return 0;
}

int TimerSetInterval(timerDevice *dev, uint64_t intervalUs)
{
    uint64_t ticks;

    if ((dev == NULL) || (dev->port == NULL)) {
        return TIMER_EINVAL;
    }
    if ((dev->state != TIMER_STATE_READY) &&
        (dev->state != TIMER_STATE_RUNNING)) {
        return TIMER_EIO;
    }
    ticks = intervalUs * (uint64_t)dev->port->clockHz / 1000000ULL;
    if ((intervalUs == 0) || (ticks < 2)) {
        return TIMER_EINVAL;
    }
    dev->loadTicks = ticks - 1ULL;
    if (dev->state == TIMER_STATE_RUNNING) {
        /* reload on the fly: stop-enable restarts the counter from the
         * new LOAD */
        int wasEnabled = dev->running;

        dev->running = 0;
        TimerRegWrite(dev, TIMER_CONTROL_REG, 0u);
        TimerProgramLoad(dev, dev->loadTicks);
        if (wasEnabled) {
            dev->running = 1;
            TimerChannelEnable(dev);
            TimerMmioBarrier();
        }
    } else {
        TimerProgramLoad(dev, dev->loadTicks);
    }
    return 0;
}

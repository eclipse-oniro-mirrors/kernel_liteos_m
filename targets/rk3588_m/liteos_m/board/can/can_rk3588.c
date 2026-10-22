/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
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
 * can_rk3588.c - real-time CAN (classic 2.0B + CAN-FD) data plane for the
 * AMP slave core on the RK3588 CAN controllers (CAN1 fea60000 / CAN2
 * fea70000). Protocol-agnostic: J1939, CANopen, proprietary or raw
 * experiments all ride the same frame-level contract (can.h v2).
 *
 * MULTI-PORT: the driver is fully instance-based. g_ports[] carries the
 * per-controller hardware profile (base/intid/ctrl page - see
 * can1_profile.h / can2_profile.h); every scrap of former file-scope
 * state (ready queue, stats, dup-suppression, bus-off count) lives
 * inside the per-port CanDevice in g_devs[]. CanInit(cfg->port_id)
 * hands out the slot; CAN1 and CAN2 run CONCURRENTLY on independent
 * fifos, irqs and ctrl pages with zero shared mutable state.
 *
 * Register map and operation sequences follow the RK3588 TRM
 * reset-mode contract:
 *   - soft reset mode = CAN_MODE cleared (no CRU access from the slave)
 *   - start: filters all-open, rx fifo enable, timestamp counter enable,
 *     MODE_FDOE|AUTO_RETX|RXSTX|COVER (+work mode bits), NBTP (+DBTP,
 *     +TDCR=0 in FD mode), then WORK_MODE
 *   - tx: TXID/TXFIC/TXDAT0..15 + the MODE_SPACE_RX handshake around
 *     CAN_CMD TX0_REQ, then bounded wait for completion (tx fifo is ONE
 *     frame deep: tx-fifo-depth = 1). FD frames set TXFIC FDF/BRS and
 *     write ceil(len/4) TXDAT words.
 *   - rx: RXFRD sequential read of the FIXED 19-word element
 *     (dlc + id + ts + 16 data words - all 16 data words are always
 *     read regardless of frame length) pops one frame from
 *     the hardware fifo (depth 6)
 *
 * RK3588 register differences vs the RK3568 port (RK3588 TRM):
 *   - RXFC fill counter sits at bit[7:5] (RK3568: bit[6:4])
 *   - DBTP (0x104) holds the FD data-phase bit timing; field widths are
 *     tighter than NBTP: sjw 1..16, ts1 1..32, ts2 1..16, brp even
 *     2..512 (stores brp/2-1 in 8 bits)
 *   - TDCR (0x108) transmitter delay compensation: kept at 0 for every
 *     data bitrate (can.h note - BRS reception with TDC off is the
 *     verified working configuration for every data bitrate this
 *     port uses)
 *   - MODE.BRSD (bit 13) is unused: BRS is requested per-frame via
 *     TXFIC
 *
 * MODE_RXSTX is the ONLY path a self-transmitted frame takes into the
 * rx fifo on this silicon (same as RK3568). LBACK is still required for
 * the self-ack (without it TX never completes on a dead bus), and
 * LBACK+RXSTX writes every frame TWICE (one element per path, close
 * together in time) - the second copy is suppressed in CanRxPopOne()
 * by fifo timestamp proximity (CAN_DUP_TS_WINDOW).
 *
 * Division of labor: the Linux master (mcs_can.ko) owns clocks, CRU
 * resets, pinmux and the shared control blocks (can_ctrl.h v2; one
 * 0x100 page per port - 0x740ff100 CAN1, 0x740ff200 CAN2); this file
 * owns everything else, including the per-port CAN interrupts (LOS
 * ArchHwiCreate configures the GICD for the owned SPI - group/
 * route/trigger/priority, per-core ownership):
 *
 *   CanInit   - validate config, gate on the master handover, enter the
 *                 soft reset mode, program filters + rx fifo + mode bits
 *                 + bit timing (arbitration solver/raw + data solver/
 *                 raw in FD mode), leave the controller in reset mode.
 *                 Legal in READY = full reconfigure; RUNNING must Stop
 *                 first.
 *   CanStart  - register the port INTID (IRQ/HYBRID only), unmask the
 *                 rx/tx/error interrupts (POLL keeps everything
 *                 masked), set WORK_MODE
 *   CanSend   - synchronous single-frame transmit with the SPACE_RX
 *                 handshake and a bounded completion wait; classic or
 *                 FD frames per the port's fd_mode
 *   CanRecv   - dequeue from the ready queue; POLL/HYBRID reaps the
 *                 hardware fifo first
 *   CanStop   - mask all interrupts, back to reset mode, deregister the
 *                 IRQ (interrupt modes), reset the queues
 *
 * Bounded waits use the ARM generic timer (CNTPCT_EL0 / CNTFRQ_EL0)
 * instead of calibrated spin loops: the slave core is CPU7 (A76 @
 * ~2.2GHz) on RK3588 and the RK3568 port's "~30 iterations/us" A55
 * calibration does not carry over.
 *
 * Memory model: no DMA, no dynamic allocation. The ready queue is a
 * static SPSC ring of CanFrame copies (producer = ISR, consumer = the
 * task, both on CPU7; head written only by the consumer, tail only by
 * the producer - the same discipline that fixed the net queue race).
 */

#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "los_arch_interrupt.h"
#include "can.h"
#include "can_ctrl.h"
#include "can1_profile.h"
#include "can2_profile.h"

/* ------------------------------------------------------------------ */
/* Register map (Rockchip CAN 2.0/FD mode; offsets relative to the     */
/* port base - same IP on CAN1 and CAN2)                               */
/* ------------------------------------------------------------------ */

#define CAN_MODE            0x00u
#define CAN_CMD             0x04u
#define CAN_STATE_REG       0x08u
#define CAN_INT             0x0cu
#define CAN_INT_MASK        0x10u
#define CAN_RX_ERR_CNT      0x34u
#define CAN_TX_ERR_CNT      0x38u
#define CAN_IDCODE          0x3cu
#define CAN_IDMASK          0x40u
#define CAN_NBTP            0x100u
#define CAN_DBTP            0x104u    /* FD data-phase bit timing */
#define CAN_TDCR            0x108u    /* transmitter delay compensation */
#define CAN_TSCC            0x10cu
#define CAN_RXFC            0x118u
#define CAN_IDCODE0         0x120u
#define CAN_IDMASK0         0x124u
#define CAN_TXFIC           0x200u
#define CAN_TXID            0x204u
#define CAN_TXDAT0          0x208u    /* TXDAT0 + 4*i, i = 0..15 (FD) */
#define CAN_RXFRD           0x400u

/* CAN_MODE bits */
#define MODE_FDOE           (1u << 15)
#define MODE_BRSD           (1u << 13)  /* RK3588 only; never set (BRS is
                                         * per-frame via TXFIC) */
#define MODE_SPACE_RX       (1u << 12)
#define MODE_AUTO_RETX      (1u << 10)
#define MODE_COVER          (1u << 8)
#define MODE_RXSTX          (1u << 5)
#define MODE_LBACK          (1u << 4)
#define MODE_SILENT         (1u << 3)
#define MODE_SELF_TEST      (1u << 2)
#define WORK_MODE           (1u << 0)

/* CAN_CMD bits */
#define CAN_TX0_REQ         (1u << 0)

/* CAN_STATE bit: bus-off flag */
#define CAN_STATE_BUSOFF    (1u << 5)

/* CAN_INT / CAN_INT_MASK bits (mask = 1 -> masked out) */
#define RX_FINISH_INT       (1u << 0)
#define TX_FINISH_INT       (1u << 1)
#define ERR_WARN_INT        (1u << 2)
#define RX_BUF_OV_INT       (1u << 3)
#define PASSIVE_ERR_INT     (1u << 4)
#define TX_LOSTARB_INT      (1u << 5)
#define BUS_ERR_INT         (1u << 6)
#define RX_FIFO_FULL_INT    (1u << 7)
#define RX_FIFO_OV_INT      (1u << 8)
#define BUS_OFF_INT         (1u << 9)

#define CAN_INT_ERROR_SET   (ERR_WARN_INT | RX_BUF_OV_INT | PASSIVE_ERR_INT | \
                             TX_LOSTARB_INT | BUS_ERR_INT | RX_FIFO_FULL_INT | \
                             RX_FIFO_OV_INT | BUS_OFF_INT)
#define CAN_INT_ENABLED     (RX_FINISH_INT | TX_FINISH_INT | CAN_INT_ERROR_SET)

/* CAN_TSCC bits */
#define TSC_ENABLE          (1u << 0)

/* CAN_RXFC bits - RK3588 fill counter sits at bit[7:5] (3-bit) */
#define FIFO_ENABLE         (1u << 0)
#define RX_FIFO_CNT_SHIFT   5
#define RX_FIFO_CNT_MASK    (0x7u << RX_FIFO_CNT_SHIFT)

/* TX/RX frame-info bits (FIC word) */
#define FORMAT_SHIFT        7
#define FORMAT_MASK         (1u << FORMAT_SHIFT)    /* 1 = extended id */
#define RTR_SHIFT           6
#define RTR_MASK            (1u << RTR_SHIFT)
#define FDF_MASK            (1u << 5)               /* CAN FD frame */
#define BRS_MASK            (1u << 4)               /* bit rate switch */
#define DLC_MASK            0xfu

/* NBTP fields (arbitration-phase bit timing) */
#define NBTP_NSJW_SHIFT     24
#define NBTP_NBRP_SHIFT     16
#define NBTP_NTSEG2_SHIFT   8
#define NBTP_NTSEG1_SHIFT   0

/* DBTP fields (FD data-phase bit timing; RK3588 widths are tighter
 * than NBTP: sjw 4 bits, brp 8 bits, ts2 4 bits, ts1 5 bits) */
#define DBTP_DSJW_SHIFT     17
#define DBTP_DBRP_SHIFT     9
#define DBTP_DTSEG2_SHIFT   5
#define DBTP_DTSEG1_SHIFT   0

/* acceptance filter width (29-bit id space) */
#define CAN_RX_FILTER_MASK  0x1fffffffu

/* loopback duplicate window in 100MHz timestamp ticks. The two fifo
 * copies of a self frame land back-to-back; a legitimate identical
 * frame cannot complete within 25us at any supported arbitration
 * bitrate (the fastest possible classic frame, dlc=0 @ 1Mbps, takes
 * ~47us) - so any identical frame inside this window is the duplicate
 * copy. FD frames switch to the data rate only AFTER arbitration, so
 * the arbitration-phase lower bound still applies. */
#define CAN_DUP_TS_WINDOW   2500u

/* ------------------------------------------------------------------ */
/* Generic-timer helpers (bounded waits on any core frequency)          */
/*                                                                     */
/* CNTPCT_EL0 is the 64-bit physical counter; CNTFRQ_EL0 the counter   */
/* frequency in Hz. Both are freely readable from EL1 where the slave  */
/* runs. The RK3568 port's calibrated spin loops (~30 iter/us on an    */
/* A55) are meaningless on the RK3588 slave core (CPU7, A76) - every   */
/* bounded wait below derives its deadline from these instead.         */
/* ------------------------------------------------------------------ */

static inline uint64_t CanCntpct(void)
{
    uint64_t v;

    /* CNTVCT, not CNTPCT: a NS-EL1 read of the physical counter freezes
     * CPU7 on this board (see net_dwmac.c) - the virtual counter runs fine
     * at the same CNTFRQ rate. */
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static inline uint32_t CanCntfrq(void)
{
    uint32_t v;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline uint64_t CanUsToTicks(uint32_t us)
{
    return ((uint64_t)CanCntfrq() * (uint64_t)us) / 1000000u;
}

static inline int CanTimeReached(uint64_t deadline)
{
    return (int64_t)(CanCntpct() - deadline) >= 0;
}

/* ------------------------------------------------------------------ */
/* Static state (contract: no dynamic allocation)                      */
/* ------------------------------------------------------------------ */

/* ready queue: lock-free SPSC ring of frame copies. Producer = the ISR
 * (drain), consumer = the task (CanRecv); both on CPU7, head written
 * only by the consumer, tail only by the producer (see net_dwmac.c for
 * the race this discipline prevents). Capacity CAN_READY_Q_NUM-1.
 * Frames are 72B (CanFrame.data[64]); 32 slots = 2.25KB per port. */
#define CAN_READY_Q_NUM     32u

/* per-controller hardware profile (immutable, from the profile headers).
 * port_id uses the PUBLIC can.h selectors; the can_ctrl.h port_index
 * the master publishes is numerically identical (1/2). */
struct can_port {
    uint32_t port_id;                   /* CAN_PORT_*_RK3588 (can.h) */
    uintptr_t base;                     /* controller MMIO base */
    uint32_t intid;                     /* GIC INTID owned by the slave */
    uint32_t irq_prio;                  /* LOS hwi priority */
    uintptr_t ctrl_phys;                /* shared ctrl block page */
    uint32_t rx_fifo_depth;             /* hardware rx fifo depth */
    uint32_t default_bitrate;           /* when cfg->bitrate == 0 */
    uint32_t default_data_bitrate;      /* when cfg->data_bitrate == 0 */
};

static const struct can_port g_ports[] = {
    { CAN_PORT_CAN1_RK3588, CAN1_BASE, CAN1_IRQ_INTID, CAN1_IRQ_LOS_PRIO,
      CAN1_CTRL_PHYS, CAN1_RX_FIFO_DEPTH, CAN1_DEFAULT_BITRATE,
      CAN1_DEFAULT_DATA_BITRATE },
    { CAN_PORT_CAN2_RK3588, CAN2_BASE, CAN2_IRQ_INTID, CAN2_IRQ_LOS_PRIO,
      CAN2_CTRL_PHYS, CAN2_RX_FIFO_DEPTH, CAN2_DEFAULT_BITRATE,
      CAN2_DEFAULT_DATA_BITRATE },
};
#define CAN_PORT_SLOTS  ((uint32_t)(sizeof(g_ports) / sizeof(g_ports[0])))

/* per-port device: ALL runtime state is instance-scoped so two ports
 * can run concurrently without any shared mutable file-scope data */
struct CanDevice {
    uint32_t state;                     /* CAN_CTRL_STATE_* (slave view) */
    uint32_t port_id;                   /* CAN_PORT_* this instance serves */
    uint32_t mode;                      /* CAN_MODE_* (runtime) */
    uint32_t work_mode;                 /* CAN_WORK_* (runtime) */
    uint32_t fd_mode;                   /* 1 = FD-capable config */
    uint32_t bus_off_count;
    uint32_t hw_mode_val;               /* latched CAN_MODE write (recover) */
    const struct can_port *port;        /* hardware profile (g_ports[slot]) */
    volatile struct can_ctrl *ctrl;     /* shared page @ port->ctrl_phys */
    CanStats stats;
    CanFrame rdy_q[CAN_READY_Q_NUM];    /* per-instance ready ring */
    volatile uint32_t rdy_head, rdy_tail;
    /* loopback duplicate suppression: previous popped frame + its fifo
     * timestamp. On this silicon LBACK+RXSTX writes the self frame into
     * the rx fifo TWICE - both copies are the same reception event, so
     * their ts word is identical. Two real receptions are at least one
     * frame time apart, i.e. thousands of 100MHz ticks at any supported
     * bitrate, so an exact ts match cannot swallow a legitimate repeat. */
    CanFrame prev_rx;
    uint32_t prev_rx_ts;
    uint32_t prev_rx_valid;
    uint32_t diag_dupmiss;              /* dup-miss diagnostic limiter
                                         * (bring-up; see CanRxPopOne) */
};

static struct CanDevice g_devs[CAN_PORT_SLOTS];    /* zero = OFFLINE */

/* map a CanDevice* back to its slot (rejects foreign pointers) */
static int CanDevSlot(const CanDevice *dev)
{
    uint32_t i;

    if (dev == NULL) {
        return -1;
    }
    for (i = 0; i < CAN_PORT_SLOTS; i++) {
        if (dev == &g_devs[i]) {
            return (int)i;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Register helpers (PA == VA identity mapping on the slave)           */
/* ------------------------------------------------------------------ */

static inline uint32_t CanRegRead(const struct CanDevice *d, uint32_t off)
{
    return *(volatile uint32_t *)(d->port->base + off);
}

static inline void CanRegWrite(const struct CanDevice *d, uint32_t off,
                                 uint32_t val)
{
    *(volatile uint32_t *)(d->port->base + off) = val;
}

static inline void CanDsb(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* LOS interrupt API                                                   */
/*                                                                     */
/* ArchHwiCreate/ArchHwiDelete live in kernel/liteos_new/arch/arm/common/
 * los_common_interrupt.c; no public header declares them (the mica irq
 * backend and net_dwmac.c call them the same way). The gic-v3 port's
 * createIrq applies the per-core ownership contract for SPIs. */
/* ------------------------------------------------------------------ */

extern UINT32 ArchHwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio,
                            HWI_MODE_T hwiMode, HWI_PROC_FUNC hwiHandler,
                            HwiIrqParam *irqParam);
extern UINT32 ArchHwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam);

/* ------------------------------------------------------------------ */
/* Bit timing solvers                                                  */
/*                                                                     */
/* bitrate = baudclk / (brp * (1 + ts1 + ts2)), brp even 2..512 (the    */
/* register stores (brp/2 - 1) - brp_inc = 2). Both phases walk brp     */
/* upward (small divisors first = finer tq resolution).                 */
/*                                                                     */
/* Arbitration phase (NBTP): ts1/ts2 1..128, N = 1+ts1+ts2 in 8..257,   */
/* sample point ~87.5% (ts1 = N*7/8, CiA recommendation).              */
/*                                                                     */
/* Data phase (DBTP, fd_mode only): RK3588 field widths cap the         */
/* segments harder - ts1 1..32, ts2 1..16 (N therefore 8..49), sample   */
/* point ~75% (ts1 = N*3/4, CiA FD data-phase recommendation).          */
/* ------------------------------------------------------------------ */

static int CanCalcTiming(uint32_t baudclk_hz, uint32_t bitrate,
                           CanBittiming *out)
{
    uint32_t div;

    if ((baudclk_hz == 0) || (bitrate == 0)) {
        return -1;
    }
    for (div = 2; div <= 512; div += 2) {
        uint32_t prod = div * bitrate;
        uint32_t n, ts1, ts2;

        if (prod == 0) {
            return -1;                 /* overflow guard (cannot happen) */
        }
        if ((baudclk_hz % prod) != 0) {
            continue;                  /* no exact solution at this brp */
        }
        n = baudclk_hz / prod;         /* tq per bit = 1 + ts1 + ts2 */
        if ((n < 8) || (n > 257)) {
            continue;
        }
        ts1 = (n * 7u) / 8u;           /* sample point ~87.5% */
        ts2 = n - 1u - ts1;
        if ((ts1 < 1) || (ts1 > 128) || (ts2 < 1) || (ts2 > 128)) {
            continue;
        }
        out->brp = div;
        out->sjw = 1;
        out->ts1 = ts1;
        out->ts2 = ts2;
        return 0;
    }
    return -1;                         /* e.g. 800k @ 100MHz: 100M/800k=125,
                                        * no even divisor exists */
}

static int CanCalcDataTiming(uint32_t baudclk_hz, uint32_t bitrate,
                                CanBittiming *out)
{
    uint32_t div;

    if ((baudclk_hz == 0) || (bitrate == 0)) {
        return -1;
    }
    for (div = 2; div <= 512; div += 2) {
        uint32_t prod = div * bitrate;
        uint32_t n, ts1, ts2;

        if (prod == 0) {
            return -1;
        }
        if ((baudclk_hz % prod) != 0) {
            continue;
        }
        n = baudclk_hz / prod;
        if ((n < 8) || (n > 49)) {
            continue;                  /* DBTP ts1<=32/ts2<=16 cap N at 49 */
        }
        ts1 = (n * 3u) / 4u;           /* data-phase sample point ~75% */
        ts2 = n - 1u - ts1;
        if ((ts1 < 1) || (ts1 > 32) || (ts2 < 1) || (ts2 > 16)) {
            continue;
        }
        out->brp = div;
        out->sjw = 1;
        out->ts1 = ts1;
        out->ts2 = ts2;
        return 0;
    }
    return -1;                         /* e.g. 4M/8M @ 100MHz have no even
                                        * divisor; 1M/2M/2.5M/5M/6.25M do */
}

/* Write NBTP from solved/raw timing (register encodings: brp stores
 * brp/2-1, segments store value-1). */
static int CanApplyTiming(const struct CanDevice *d, const CanBittiming *t)
{
    uint32_t reg;

    if ((t->brp < 2) || (t->brp > 512) || ((t->brp & 1u) != 0) ||
        (t->sjw < 1) || (t->sjw > 128) ||
        (t->ts1 < 1) || (t->ts1 > 128) || (t->ts2 < 1) || (t->ts2 > 128)) {
        return -1;
    }
    reg = ((t->brp / 2u - 1u) << NBTP_NBRP_SHIFT) |
          ((t->sjw - 1u) << NBTP_NSJW_SHIFT) |
          ((t->ts1 - 1u) << NBTP_NTSEG1_SHIFT) |
          ((t->ts2 - 1u) << NBTP_NTSEG2_SHIFT);
    CanRegWrite(d, CAN_NBTP, reg);
    return 0;
}

/* Write DBTP from solved/raw FD data-phase timing + park TDCR at 0.
 * DBTP validates against the TIGHTER RK3588 field widths (see the
 * solver block comment); TDCR = 0 keeps transmitter delay compensation
 * disabled for every data bitrate (can.h note). */
static int CanApplyDataTiming(const struct CanDevice *d,
                                 const CanBittiming *t)
{
    uint32_t reg;

    if ((t->brp < 2) || (t->brp > 512) || ((t->brp & 1u) != 0) ||
        (t->sjw < 1) || (t->sjw > 16) ||
        (t->ts1 < 1) || (t->ts1 > 32) || (t->ts2 < 1) || (t->ts2 > 16)) {
        return -1;
    }
    reg = ((t->brp / 2u - 1u) << DBTP_DBRP_SHIFT) |
          ((t->sjw - 1u) << DBTP_DSJW_SHIFT) |
          ((t->ts1 - 1u) << DBTP_DTSEG1_SHIFT) |
          ((t->ts2 - 1u) << DBTP_DTSEG2_SHIFT);
    CanRegWrite(d, CAN_DBTP, reg);
    CanRegWrite(d, CAN_TDCR, 0);
    return 0;
}

/* ------------------------------------------------------------------ */
/* RX fifo drain (ISR / Recv in POLL+HYBRID)                           */
/* ------------------------------------------------------------------ */

/* Pop one frame from the hardware fifo into the ready queue.
 * Returns 1 when a frame was queued, 0 when the fifo is empty or the
 * frame was suppressed, -1 on a dropped/invalid frame. */
static int CanRxPopOne(struct CanDevice *d)
{
    uint32_t dlc_word = CanRegRead(d, CAN_RXFRD);
    uint32_t id_word = CanRegRead(d, CAN_RXFRD);
    uint32_t ts = CanRegRead(d, CAN_RXFRD);
    uint32_t data[16];
    CanFrame f;
    uint32_t i, len;

    /* the fifo pointer advances per 32-bit read: drain the FULL fixed
     * 19-word element (dlc + id + ts + 16 data words) regardless of
     * frame length, classic or FD - a partial read would desync the
     * fifo. */
    for (i = 0; i < 16; i++) {
        data[i] = CanRegRead(d, CAN_RXFRD);
    }

    /* silicon may surface an empty frame - skip it (kernel parity) */
    if ((dlc_word == 0) && (id_word == 0)) {
        return 0;
    }

    f.id = id_word & CAN_RX_FILTER_MASK;
    f.flags = 0;
    if ((dlc_word & FORMAT_MASK) != 0) {
        f.flags |= CAN_FRAME_EFF;
    }
    if ((dlc_word & RTR_MASK) != 0) {
        f.flags |= CAN_FRAME_RTR;
    }
    f.dlc = dlc_word & DLC_MASK;
    f.reserved = 0;

    if ((dlc_word & FDF_MASK) != 0) {
        /* CAN-FD frame: legal only when the port runs FD; classic-mode
         * DBTP was never programmed so a rate-switched FD frame cannot
         * sample correctly anyway. Drop with a protocol error. */
        if (d->fd_mode == 0) {
            d->stats.rx_errors++;
            return -1;
        }
        f.flags |= CAN_FRAME_FDF;
        if ((dlc_word & BRS_MASK) != 0) {
            f.flags |= CAN_FRAME_BRS;
        }
        len = CanFdDlc2Len(f.dlc);
    } else {
        /* classic frame: DLC 9..15 is reserved */
        if (f.dlc > 8) {
            d->stats.rx_errors++;
            return -1;
        }
        len = f.dlc;
    }

    for (i = 0; i < len; i++) {
        f.data[i] = (uint8_t)(data[i / 4] >> ((i % 4) * 8));
    }

    /* loopback duplicate: the second fifo copy of the same self frame,
     * written within CAN_DUP_TS_WINDOW of the first (RTR echoes carry
     * no data - compare the header only) */
    if (d->prev_rx_valid &&
        ((uint32_t)(ts - d->prev_rx_ts) <= CAN_DUP_TS_WINDOW) &&
        (f.id == d->prev_rx.id) && (f.flags == d->prev_rx.flags) &&
        (f.dlc == d->prev_rx.dlc) &&
        (((f.flags & CAN_FRAME_RTR) != 0) ||
         (memcmp(f.data, d->prev_rx.data, len) == 0))) {
        d->stats.rx_dup_suppressed++;
        return 0;                       /* consumed, not delivered */
    }
    /* same frame again OUTSIDE the window: not the duplicate copy -
     * surfaced for bring-up (should never happen in the demo) */
    if ((d->diag_dupmiss < 40) && d->prev_rx_valid &&
        (f.id == d->prev_rx.id) && (f.flags == d->prev_rx.flags) &&
        (f.dlc == d->prev_rx.dlc) && ((uint32_t)(ts - d->prev_rx_ts) > CAN_DUP_TS_WINDOW)) {
        d->diag_dupmiss++;
        PRINT_ERR("[can][diag] dup-miss: id=%x ts=%08x prev=%08x\n",
                  f.id, ts, d->prev_rx_ts);
    }
    d->prev_rx = f;
    d->prev_rx_ts = ts;
    d->prev_rx_valid = 1;

    if (((d->rdy_tail + 1u) % CAN_READY_Q_NUM) == d->rdy_head) {
        d->stats.rx_overflow++;         /* queue full: drop */
        d->stats.rx_dropped++;
        return -1;
    }
    d->rdy_q[d->rdy_tail] = f;
    CanDsb();                          /* frame visible before tail moves */
    d->rdy_tail = (d->rdy_tail + 1u) % CAN_READY_Q_NUM;
    return 1;
}

/* ~20us settle spin: the RXFC fill counter lags the pop (RK3568
 * erratum 5 at the 100MHz bit clock), so the drain loop re-reads it
 * only after this window. The RK3588 IP is the same family - keeping
 * the settle is harmless even if its counter does not lag. Generic-
 * timer based: core frequency independent. */
static void CanSettleDelay(void)
{
    uint64_t deadline = CanCntpct() + CanUsToTicks(20);

    while (!CanTimeReached(deadline)) {
        __asm__ volatile("nop");
    }
}

/* Drain everything the hardware fifo holds (depth 6, cnt is 3-bit).
 * One settle spin between pops: re-reading RXFC immediately after a
 * pop can still show the pre-pop count and yield a ghost element. */
static void CanRxDrain(struct CanDevice *d)
{
    uint32_t pass = 0;

    while (pass < 2 * d->port->rx_fifo_depth + 2) {
        uint32_t fc = (CanRegRead(d, CAN_RXFC) & RX_FIFO_CNT_MASK) >>
                      RX_FIFO_CNT_SHIFT;
        if (fc == 0) {
            break;
        }
        (VOID)CanRxPopOne(d);
        CanSettleDelay();
        pass++;
    }
}

/* ------------------------------------------------------------------ */
/* Interrupt service (interrupt mode)                                  */
/*                                                                     */
/* ArchHwiCreate takes a plain VOID(VOID) entry, so each port gets a   */
/* tiny trampoline into the shared body below (g_isr_tab).            */
/* ------------------------------------------------------------------ */

static VOID CanIsrCommon(struct CanDevice *d)
{
    uint32_t isr = CanRegRead(d, CAN_INT);

    /* W1C on the bits we handle */
    CanRegWrite(d, CAN_INT, isr & CAN_INT_ENABLED);

    d->stats.irq_count++;

    if ((isr & RX_FINISH_INT) != 0) {
        CanRxDrain(d);
    }
    if ((isr & (BUS_OFF_INT | RX_FIFO_OV_INT | RX_BUF_OV_INT)) != 0) {
        if ((isr & BUS_OFF_INT) != 0) {
            d->bus_off_count++;
        }
        d->stats.rx_errors++;
    }
    if ((isr & TX_LOSTARB_INT) != 0) {
        d->stats.tx_errors++;
    }
    /* TX_FINISH: CanSend is synchronous and clears its own completion;
     * nothing to do here (the W1C above releases the irq line). */
}

static VOID Can1Isr(VOID)
{
    CanIsrCommon(&g_devs[0]);
}

static VOID Can2Isr(VOID)
{
    CanIsrCommon(&g_devs[1]);
}

/* slot-indexed ISR entry table (CanStart) */
static const HWI_PROC_FUNC g_isr_tab[CAN_PORT_SLOTS] = { Can1Isr, Can2Isr };

/* ------------------------------------------------------------------ */
/* Bounded TX waits (tx fifo is ONE frame deep)                         */
/*                                                                     */
/* timeout_us == 0 polls once and fails fast (RK3568-port parity);     */
/* otherwise the deadline comes from the generic timer.                */
/* ------------------------------------------------------------------ */

static int CanWaitTxClear(struct CanDevice *d, uint32_t timeout_us)
{
    uint64_t deadline;

    if ((CanRegRead(d, CAN_CMD) & CAN_TX0_REQ) == 0) {
        return 0;
    }
    if (timeout_us == 0) {
        return -1;
    }
    deadline = CanCntpct() + CanUsToTicks(timeout_us);
    while ((CanRegRead(d, CAN_CMD) & CAN_TX0_REQ) != 0) {
        if (CanTimeReached(deadline)) {
            return -1;
        }
    }
    return 0;
}

/* Engine-freeze recovery (2026-10-16, fd-poll -110 cascade): the TX
 * engine can stop mid-run with TX0_REQ pending forever while the error
 * counters stay at zero, no bus-off, and the 100MHz can clock verified
 * enabled from the master (clk_summary). Only a MODE=0 round trip
 * (soft reset) is left to try. Costs the stuck frame's echo.
 *
 * 2026-10-17: the restore is now the verified two-step (config bits
 * written first, WORK_MODE raised last). A
 * single one-shot MODE write of the full hw_mode_val measured as WORK
 * silently dropping (read back 0x853c), which left the engine out of
 * work mode -> the very next frame froze again (fd-poll tx#9 after a
 * "revived" recovery, serial_verify1 2026-10-17). */
static int CanTxRecover(struct CanDevice *d)
{
    uint64_t deadline;
    uint32_t mode;

    PRINT_ERR("[can][recover] p%u: TX0_REQ stuck (MODE=%#x CMD=%#x ST=%#x) - soft reset\n",
              d->port_id, CanRegRead(d, CAN_MODE), CanRegRead(d, CAN_CMD),
              CanRegRead(d, CAN_STATE_REG));
    CanRegWrite(d, CAN_MODE, 0);
    deadline = CanCntpct() + CanUsToTicks(1000u);
    while (!CanTimeReached(deadline)) {
    }
    /* step 1: config bits only (no WORK) */
    CanRegWrite(d, CAN_MODE, d->hw_mode_val & ~WORK_MODE);
    deadline = CanCntpct() + CanUsToTicks(10u);
    while (!CanTimeReached(deadline)) {
    }
    /* step 2: raise WORK_MODE last, like set_normal_mode() */
    mode = CanRegRead(d, CAN_MODE) | WORK_MODE;
    CanRegWrite(d, CAN_MODE, mode);
    if ((CanRegRead(d, CAN_MODE) & WORK_MODE) == 0) {
        PRINT_ERR("[can][recover] p%u: WORK_MODE dropped after write (MODE=%#x)\n",
                  d->port_id, CanRegRead(d, CAN_MODE));
        return -1;
    }
    if ((CanRegRead(d, CAN_CMD) & CAN_TX0_REQ) != 0) {
        PRINT_ERR("[can][recover] p%u: still stuck after soft reset\n", d->port_id);
        return -1;
    }
    PRINT_ERR("[can][recover] p%u: revived (MODE=%#x)\n",
              d->port_id, CanRegRead(d, CAN_MODE));
    return 0;
}

/* bring-up diagnostic: dump the port's key registers (tx -110 cascade
 * under concurrent net traffic is under investigation - this tells us
 * whether the controller went bus-off, error-passive, TXFIFO-stuck, or
 * the bit timing registers were disturbed). Exported, no header entry:
 * the demo declares it extern. */
void CanDiagDump(const CanDevice *dev, const char *tag)
{
    if ((dev == NULL) || (dev->port == NULL)) {
        return;
    }
    PRINTK("[can] DIAG %s p%u: MODE=%#x CMD=%#x STATE=%#x INT=%#x MASK=%#x\n",
           tag, dev->port_id,
           CanRegRead(dev, CAN_MODE), CanRegRead(dev, CAN_CMD),
           CanRegRead(dev, CAN_STATE_REG), CanRegRead(dev, CAN_INT),
           CanRegRead(dev, CAN_INT_MASK));
    PRINTK("[can] DIAG %s p%u: RXERR=%#x TXERR=%#x RXFC=%#x TXFIC=%#x NBTP=%#x DBTP=%#x\n",
           tag, dev->port_id,
           CanRegRead(dev, CAN_RX_ERR_CNT), CanRegRead(dev, CAN_TX_ERR_CNT),
           CanRegRead(dev, CAN_RXFC), CanRegRead(dev, CAN_TXFIC),
           CanRegRead(dev, CAN_NBTP), CanRegRead(dev, CAN_DBTP));
    PRINTK("[can] DIAG %s p%u: ERRCODE=%#x TXCHK_FIC=%#x\n",
           tag, dev->port_id,
           CanRegRead(dev, 0x2cu), CanRegRead(dev, 0x50u));
}

/* ------------------------------------------------------------------ */
/* Contract implementation                                            */
/* ------------------------------------------------------------------ */

int CanInit(const CanConfig *cfg, CanDevice **dev)
{
    volatile struct can_ctrl *ctrl;
    CanBittiming timing;
    CanBittiming data_timing;
    uint32_t mode_val;
    const struct can_port *port;
    struct CanDevice *d;
    uint32_t slot;
    int i;

    if ((cfg == NULL) || (dev == NULL)) {
        return CAN_EINVAL;
    }
    if ((cfg->mode > CAN_MODE_HYBRID) ||
        (cfg->work_mode > CAN_WORK_SILENT_LB)) {
        return CAN_EINVAL;
    }

    /* --- port lookup: cfg->port_id selects the controller --- */
    port = NULL;
    for (slot = 0; slot < CAN_PORT_SLOTS; slot++) {
        if (g_ports[slot].port_id == cfg->port_id) {
            port = &g_ports[slot];
            break;
        }
    }
    if (port == NULL) {
        return CAN_ENODEV;
    }
    d = &g_devs[slot];
    if (d->state == CAN_CTRL_STATE_RUNNING) {
        return CAN_EBUSY;               /* running: Stop first */
    }
    /* bind the profile BEFORE the first MMIO below: the register
     * helpers route through d->port->base and CanInit's early code
     * paths (soft reset, filters, fifo) run long before the tail of
     * this function assigns the rest of the device state */
    d->port = port;

    /* --- master handover gate (MUST run before any CAN MMIO) ---
     * CAN MMIO with the clocks off (mcs_can.ko not loaded / removed)
     * stalls the AXI bus on this SoC and hangs the core. The ctrl block
     * lives in the MCS shared window (RAM, always safe to read); the ko
     * publishes master_state=READY only after clocks/pinmux are up and
     * resets it to OFFLINE in remove(). */
    ctrl = (volatile struct can_ctrl *)port->ctrl_phys;
    if ((ctrl->magic != CAN_CTRL_MAGIC) ||
        (ctrl->version != CAN_CTRL_VERSION) ||
        ((ctrl->master_state != CAN_CTRL_STATE_READY) &&
         (ctrl->master_state != CAN_CTRL_STATE_RUNNING))) {
        PRINT_ERR("[can] master mcs_can.ko not up (ctrl invalid): CAN%u "
                  "clocks off, refusing register access\n", port->port_id);
        return CAN_ENODEV;
    }
    /* can_ctrl.h port_index is numerically identical to the public
     * can.h port ids (1/2) - a plain compare is the contract check */
    if (ctrl->port_index != port->port_id) {
        return CAN_ENODEV;
    }
    /* platform facts cross-check (informational: the address is
     * compiled in on both sides) */
    if ((ctrl->base_phys != (uint32_t)port->base) ||
        (ctrl->intid != port->intid)) {
        PRINT_ERR("[can] ctrl mismatch on CAN%u: base=0x%x intid=%u\n",
                  port->port_id, ctrl->base_phys, ctrl->intid);
        return CAN_ENODEV;
    }
    /* FD capability gate: fd_mode requires the master-published
     * fd_capable (the RK3588 master always sets it; guards a stale
     * master ko) */
    if ((cfg->fd_mode != 0) && (ctrl->fd_capable == 0)) {
        PRINT_ERR("[can] CAN%u: fd_mode requested but master reports "
                  "non-FD silicon\n", port->port_id);
        return CAN_ENOTSUP;
    }

    /* --- bit timing, arbitration phase: raw wins, else solve from the
     *     published baudclk --- */
    if (cfg->use_raw_timing != 0) {
        timing = cfg->timing;
    } else {
        uint32_t bitrate = cfg->bitrate;
        if (bitrate == 0) {
            bitrate = port->default_bitrate;
        }
        if (CanCalcTiming(ctrl->baudclk_hz, bitrate, &timing) != 0) {
            PRINT_ERR("[can] no bit timing: %u bps @ %u Hz "
                      "(800k is unreachable at 100MHz)\n",
                      bitrate, ctrl->baudclk_hz);
            return CAN_EINVAL;
        }
    }

    /* --- bit timing, FD data phase (fd_mode only): same style against
     *     the same bit clock (single CLK_CANx serves both phases) --- */
    if (cfg->fd_mode != 0) {
        if (cfg->use_raw_data_timing != 0) {
            data_timing = cfg->data_timing;
        } else {
            uint32_t dbrate = cfg->data_bitrate;
            if (dbrate == 0) {
                dbrate = port->default_data_bitrate;
            }
            if (CanCalcDataTiming(ctrl->baudclk_hz, dbrate,
                                     &data_timing) != 0) {
                PRINT_ERR("[can] no FD data timing: %u bps @ %u Hz "
                          "(4M/8M are unreachable at 100MHz)\n",
                          dbrate, ctrl->baudclk_hz);
                return CAN_EINVAL;
            }
        }
    }

    /* --- enter the soft reset mode (all config happens there) --- */
    CanRegWrite(d, CAN_MODE, 0);

    /* all interrupts masked, pending state cleared */
    CanRegWrite(d, CAN_INT_MASK, 0xffffu);
    CanRegWrite(d, CAN_INT, 0xffffu);

    /* --- acceptance filters: all-open, or the single code/mask bank.
     *     hw accepts id iff (id & ~mask) == (code & ~mask): mask bit 1
     *     = don't care, 0 = must match code. mask = 0 therefore means
     *     "exactly filter_code" (can.h contract); mask = CAN_RX_FILTER_
     *     MASK (all ones) accepts everything. --- */
    if (cfg->filter_en != 0) {
        CanRegWrite(d, CAN_IDCODE, cfg->filter_code & CAN_RX_FILTER_MASK);
        CanRegWrite(d, CAN_IDMASK, cfg->filter_mask & CAN_RX_FILTER_MASK);
    } else {
        CanRegWrite(d, CAN_IDCODE, 0);
        CanRegWrite(d, CAN_IDMASK, CAN_RX_FILTER_MASK);
    }
    /* the 5 auxiliary banks stay all-open (kernel start parity) */
    for (i = 0; i < 5; i++) {
        CanRegWrite(d, CAN_IDCODE0 + (uint32_t)i * 8, 0);
        CanRegWrite(d, CAN_IDMASK0 + (uint32_t)i * 8, CAN_RX_FILTER_MASK);
    }

    /* --- rx fifo enable + timestamp counter enable (every rx fifo
     *     element carries a ts word) --- */
    CanRegWrite(d, CAN_RXFC, CanRegRead(d, CAN_RXFC) | FIFO_ENABLE);
    CanRegWrite(d, CAN_TSCC, CanRegRead(d, CAN_TSCC) | TSC_ENABLE);

    /* --- mode bits: FDOE (required by the IP for classic AND FD
     *     operation), auto-retransmit, RXSTX and
     *     COVER (rx fifo overwrite instead of overload frames), then
     *     the work mode. BRSD (RK3588) is intentionally never set.
     *     LOOPBACK silicon facts (measured on the RK3568 board, same
     *     IP family - re-verify on the first RK3588 board run):
     *     - without LBACK the controller never self-acks: TX0_REQ stays
     *       pending and Send() returns -110 on a dead bus
     *     - RXSTX is the ONLY path a self frame takes into the rx fifo:
     *       LBACK|SILENT|SELF_TEST without it never fills the fifo
     *       (fc stays 0, all phases rx d0)
     *     - exactly ONE TXE event is generated per frame (no double
     *       transmission), but with LBACK+RXSTX every frame shows up
     *       as TWO rx fifo elements - the copies land close together
     *       and the flaky fifo fill counter (erratum 5 at 100MHz)
     *       spreads their delivery over two drains
     *     CanRxPopOne() suppresses the second copy by fifo
     *     timestamp proximity (see CAN_DUP_TS_WINDOW). */
    mode_val = MODE_FDOE | MODE_AUTO_RETX | MODE_RXSTX | MODE_COVER;
    switch (cfg->work_mode) {
    case CAN_WORK_NORMAL:
        break;
    case CAN_WORK_LOOPBACK:
    case CAN_WORK_SILENT_LB:
        mode_val |= MODE_SELF_TEST | MODE_LBACK | MODE_SILENT;
        break;
    case CAN_WORK_SILENT:
        mode_val |= MODE_SILENT;
        break;
    default:
        return CAN_EINVAL;
    }
    CanRegWrite(d, CAN_MODE, mode_val);
    d->hw_mode_val = mode_val;

    /* --- bit timing (valid in reset mode only): arbitration phase
     *     always, FD data phase when fd_mode --- */
    if (CanApplyTiming(d, &timing) != 0) {
        PRINT_ERR("[can] invalid raw timing brp=%u sjw=%u ts1=%u ts2=%u\n",
                  timing.brp, timing.sjw, timing.ts1, timing.ts2);
        return CAN_EINVAL;
    }
    if (cfg->fd_mode != 0) {
        if (CanApplyDataTiming(d, &data_timing) != 0) {
            PRINT_ERR("[can] invalid raw data timing brp=%u sjw=%u "
                      "ts1=%u ts2=%u (data caps: sjw<=16 ts1<=32 ts2<=16)\n",
                      data_timing.brp, data_timing.sjw,
                      data_timing.ts1, data_timing.ts2);
            return CAN_EINVAL;
        }
    }

    /* --- software state --- */
    (VOID)memset(&d->stats, 0, sizeof(d->stats));
    d->bus_off_count = 0;
    d->rdy_head = 0;
    d->rdy_tail = 0;
    d->prev_rx_valid = 0;
    d->diag_dupmiss = 0;
    d->port_id = cfg->port_id;
    d->mode = cfg->mode;
    d->work_mode = cfg->work_mode;
    d->fd_mode = (cfg->fd_mode != 0) ? 1u : 0u;
    d->port = port;
    d->ctrl = ctrl;
    d->state = CAN_CTRL_STATE_READY;

    ctrl->slave_state = CAN_CTRL_STATE_READY;

    *dev = d;
    return 0;
}

int CanStart(CanDevice *dev)
{
    int slot = CanDevSlot(dev);

    if (slot < 0) {
        return CAN_EINVAL;
    }
    if (dev->state == CAN_CTRL_STATE_RUNNING) {
        return 0;                       /* idempotent */
    }
    if (dev->state != CAN_CTRL_STATE_READY) {
        return CAN_EIO;                 /* FAULT: needs Stop first */
    }

    if (dev->mode != CAN_MODE_POLL) {
        UINT32 hwiRet = ArchHwiCreate((HWI_HANDLE_T)dev->port->intid,
                          (HWI_PRIOR_T)dev->port->irq_prio,
                          HWI_MODE_TRIGGER_LEVEL,
                          g_isr_tab[slot], (HwiIrqParam *)0);
        if (hwiRet != LOS_OK) {
            /* error code + the dispatch slot state: OS_ERRNO_HWI_ALREADY_
             * CREATED means something claimed INTID 374 earlier; dump the
             * current form entry to identify the squatter. */
            extern HWI_PROC_FUNC g_hwiForm[];
            PRINT_ERR("[can] ArchHwiCreate(%u) failed ret=0x%x form374=%p\n",
                      dev->port->intid, hwiRet,
                      (void *)g_hwiForm[dev->port->intid]);
            return CAN_EIO;
        }
    }

    /* clear anything latched, then unmask rx/tx/errors (POLL keeps the
     * line silent - Recv() drives everything) */
    CanRegWrite(dev, CAN_INT, 0xffffu);
    if (dev->mode != CAN_MODE_POLL) {
        CanRegWrite(dev, CAN_INT_MASK, ~CAN_INT_ENABLED);
    } else {
        CanRegWrite(dev, CAN_INT_MASK, 0xffffu);
    }

    /* leave the soft reset mode: the controller joins the bus */
    CanRegWrite(dev, CAN_MODE, CanRegRead(dev, CAN_MODE) | WORK_MODE);

    dev->state = CAN_CTRL_STATE_RUNNING;
    if ((dev->ctrl->magic == CAN_CTRL_MAGIC) &&
        (dev->ctrl->version == CAN_CTRL_VERSION)) {
        dev->ctrl->slave_state = CAN_CTRL_STATE_RUNNING;
    }
    return 0;
}

int CanStop(CanDevice *dev, uint32_t timeout_us)
{
    if (CanDevSlot(dev) < 0) {
        return CAN_EINVAL;
    }
    if (dev->state != CAN_CTRL_STATE_RUNNING) {
        return 0;
    }
    (VOID)timeout_us;                   /* no DMA to quiesce: the reset
                                         * mode stops the engine at once */

    /* mask everything first so the ISR cannot fire mid-stop */
    CanRegWrite(dev, CAN_INT_MASK, 0xffffu);
    CanRegWrite(dev, CAN_INT, 0xffffu);

    /* back to the soft reset mode: off the bus, engines idle */
    CanRegWrite(dev, CAN_MODE, CanRegRead(dev, CAN_MODE) & ~WORK_MODE);
    CanDsb();

    if (dev->mode != CAN_MODE_POLL) {
        /* ArchHwiDelete masks the SPI at the GICD (per-core ownership) */
        (VOID)ArchHwiDelete((HWI_HANDLE_T)dev->port->intid, (HwiIrqParam *)0);
    }

    dev->rdy_head = 0;
    dev->rdy_tail = 0;

    dev->state = CAN_CTRL_STATE_READY;
    if ((dev->ctrl->magic == CAN_CTRL_MAGIC) &&
        (dev->ctrl->version == CAN_CTRL_VERSION)) {
        dev->ctrl->slave_state = CAN_CTRL_STATE_READY;
    }
    return 0;
}

int CanSend(CanDevice *dev, const CanFrame *frame, uint32_t timeout_us)
{
    uint32_t id, fic, len, words, w;
    int is_eff, is_rtr, is_fdf, is_brs;

    if ((CanDevSlot(dev) < 0) || (frame == NULL)) {
        return CAN_EINVAL;
    }
    if (dev->state != CAN_CTRL_STATE_RUNNING) {
        return CAN_EIO;
    }

    /* --- frame validation (classic vs FD rules) --- */
    is_eff = (frame->flags & CAN_FRAME_EFF) != 0;
    is_rtr = (frame->flags & CAN_FRAME_RTR) != 0;
    is_fdf = (frame->flags & CAN_FRAME_FDF) != 0;
    is_brs = (frame->flags & CAN_FRAME_BRS) != 0;

    if (is_brs && !is_fdf) {
        return CAN_EINVAL;              /* BRS only exists inside FD */
    }
    if (is_fdf) {
        if (dev->fd_mode == 0) {
            return CAN_ENOTSUP;         /* port configured classic-only */
        }
        if (is_rtr) {
            return CAN_EINVAL;          /* remote frames are classic-only */
        }
        if (frame->dlc > 15) {
            return CAN_EMSGSIZE;
        }
        len = CanFdDlc2Len(frame->dlc);
    } else {
        if (frame->dlc > 8) {
            return CAN_EMSGSIZE;
        }
        len = frame->dlc;
    }
    if (is_eff) {
        if (frame->id > 0x1fffffffu) {
            return CAN_EINVAL;
        }
    } else {
        if (frame->id > 0x7ffu) {
            return CAN_EINVAL;
        }
    }

    /* tx fifo is ONE frame deep: wait for any in-flight frame first */
    if (CanWaitTxClear(dev, timeout_us) != 0) {
        /* engine-freeze recovery: retry once after a soft reset. The
         * stuck frame's echo is lost (the demo counts 1 timeout); every
         * following frame goes out normally again. */
        if (CanTxRecover(dev) != 0) {
            dev->stats.tx_timeouts++;
            return CAN_ETIMEDOUT;
        }
    }

    /* frame assembly: TXID raw, format/rtr/fdf/brs/dlc in TXFIC,
     * then ceil(len/4) TXDAT words */
    id = frame->id;
    fic = frame->dlc & DLC_MASK;
    if (is_eff) {
        fic |= FORMAT_MASK;
    }
    if (is_rtr) {
        fic |= RTR_MASK;
    }
    if (is_fdf) {
        fic |= FDF_MASK;
        if (is_brs) {
            fic |= BRS_MASK;
        }
    }
    CanRegWrite(dev, CAN_TXID, id);
    CanRegWrite(dev, CAN_TXFIC, fic);
    words = (len + 3u) / 4u;
    for (w = 0; w < words; w++) {
        uint32_t word = 0;
        uint32_t i;

        for (i = 0; i < 4; i++) {
            uint32_t idx = w * 4u + i;
            if (idx < len) {
                word |= (uint32_t)frame->data[idx] << (i * 8);
            }
        }
        CanRegWrite(dev, CAN_TXDAT0 + w * 4u, word);
    }
    CanDsb();

    /* launch with the SPACE_RX handshake (kernel sequence):
     * set MODE_SPACE_RX, raise TX0_REQ, clear MODE_SPACE_RX again.
     * MODE_RXSTX is NOT touched here: it is latched permanently in
     * CanInit (raising it only around the command leaves a window of a
     * few microseconds - the frame is still on the wire when the bit is
     * cleared again, so the echo never lands in the rx fifo). */
    CanRegWrite(dev, CAN_MODE, CanRegRead(dev, CAN_MODE) | MODE_SPACE_RX);
    CanRegWrite(dev, CAN_CMD, CAN_TX0_REQ);
    CanRegWrite(dev, CAN_MODE, CanRegRead(dev, CAN_MODE) & ~MODE_SPACE_RX);
    dev->stats.tx_accepted++;

    /* bounded wait for THIS frame to complete (TX0_REQ self-clears) */
    if (CanWaitTxClear(dev, timeout_us) != 0) {
        dev->stats.tx_timeouts++;
        return CAN_ETIMEDOUT;
    }
    /* consume the TX_FINISH status (W1C) so it cannot stack up */
    CanRegWrite(dev, CAN_INT, TX_FINISH_INT);

    /* clear the command register after the REQ bit self-cleared:
     * write CAN_CMD=0 once !(CAN_CMD & 0x3) holds. Without this
     * handshake the REQ bit
     * eventually sticks (TX0_REQ pending forever, zero error counters,
     * loopback engine dead; reproduced 2026-10-16 once net TX traffic
     * runs concurrently - fd-poll -110 cascade). */
    CanRegWrite(dev, CAN_CMD, 0);

    dev->stats.tx_completed++;
    return 0;
}

int CanRecv(CanDevice *dev, CanFrame *frame)
{
    if ((CanDevSlot(dev) < 0) || (frame == NULL)) {
        return CAN_EINVAL;
    }
    if (dev->state != CAN_CTRL_STATE_RUNNING) {
        return CAN_EIO;
    }

    /* POLL: Recv() is the only reaper. HYBRID: the ISR reaps ahead of
     * us; reaping here too is idempotent and backstops a dropped IRQ. */
    if (dev->mode != CAN_MODE_IRQ) {
        CanRxDrain(dev);
    }

    while (dev->rdy_head != dev->rdy_tail) {
        *frame = dev->rdy_q[dev->rdy_head];
        dev->rdy_head = (dev->rdy_head + 1u) % CAN_READY_Q_NUM;
        dev->stats.rx_delivered++;
        return 0;
    }
    return CAN_EAGAIN;
}

int CanGetBusState(CanDevice *dev, CanBusInfo *info)
{
    uint32_t rxerr, txerr, state_reg;

    if ((CanDevSlot(dev) < 0) || (info == NULL)) {
        return CAN_EINVAL;
    }

    if (dev->state != CAN_CTRL_STATE_RUNNING) {
        info->state = CAN_BUS_STOPPED;
        info->rx_err_cnt = 0;
        info->tx_err_cnt = 0;
        info->bus_off_count = dev->bus_off_count;
        return 0;
    }

    rxerr = CanRegRead(dev, CAN_RX_ERR_CNT) & 0xffu;
    txerr = CanRegRead(dev, CAN_TX_ERR_CNT) & 0xffu;
    state_reg = CanRegRead(dev, CAN_STATE_REG);

    if ((state_reg & CAN_STATE_BUSOFF) != 0) {
        info->state = CAN_BUS_BUS_OFF;
    } else if ((rxerr >= 128) || (txerr >= 128)) {
        info->state = CAN_BUS_ERROR_PASSIVE;
    } else if ((rxerr >= 96) || (txerr >= 96)) {
        info->state = CAN_BUS_ERROR_WARNING;
    } else {
        info->state = CAN_BUS_ERROR_ACTIVE;
    }
    info->rx_err_cnt = rxerr;
    info->tx_err_cnt = txerr;
    info->bus_off_count = dev->bus_off_count;
    return 0;
}

int CanGetStats(CanDevice *dev, CanStats *stats)
{
    if ((CanDevSlot(dev) < 0) || (stats == NULL)) {
        return CAN_EINVAL;
    }
    *stats = dev->stats;
    stats->bus_off_count = dev->bus_off_count;
    return 0;
}

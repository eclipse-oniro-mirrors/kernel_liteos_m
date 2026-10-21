/*
 * copyright (c) 2026 huawei device co., ltd.
 * licensed under the apache license, version 2.0 (the "license");
 * you may not use this file except in compliance with the license.
 * you may obtain a copy of the license at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * unless required by applicable law or agreed to in writing, software
 * distributed under the license is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * see the license for the specific language governing permissions and
 * limitations under the license.
 *
 * can_rk3568.c - CAN (classic 2.0B + FD) driver for the RK3568 AMP slave
 * core (CAN1 fe580000), protocol-agnostic frame level (contract: can.h).
 * instances: g_ports[] carries the per-controller profile (can1_profile.h);
 * all runtime state lives in the per-port canDevice, so ports run
 * concurrently with no shared mutable state.
 * register programming follows the TRM reset-mode contract: soft reset
 * mode = CAN_MODE cleared; start = filters open + rx fifo + mode bits +
 * NBTP, then WORK_MODE; tx = TXID/TXFIC/TXDAT + the SPACE_RX handshake
 * (the tx fifo is ONE frame deep); rx = sequential RXFRD read.
 * FD: MODE bit15 (FDOE) is set unconditionally - the FD register set
 * shares the classic addresses and only adds DBTP/TDC, the FDF/BRS bits
 * and the len<->DLC mapping. rk3568 v3 FD receive carries known errata:
 * FD on an external bus is pilot-stage only (rk3588 is the reliable FD
 * IP). the RTL version is printed at init.
 * MODE_RXSTX is the only path a self-transmitted frame takes into the
 * rx fifo on this silicon; with LBACK the frame is written TWICE and
 * the duplicate is suppressed in CanRxPopOne() by fifo timestamp.
 * the linux master (mcs_can.ko) owns clocks/resets/pinmux and must be
 * loaded before the slave touches the port (board/DRIVER_GUIDE.md) -
 * with the clocks gated any CAN MMIO hangs the core. the slave owns the
 * data path and its interrupts (ArchHwiCreate, per-core GICD route).
 * no DMA, no dynamic allocation: the ready queue is a static SPSC ring
 * of canFrame copies (producer = ISR, consumer = the task).
 */

#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "los_arch_interrupt.h"
#include "can.h"
#include "can1_profile.h"

/* ------------------------------------------------------------------ */
/* register map (rockchip CAN 2.0 mode; offsets relative to the port   */
/* base - same IP across ports)                                        */
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
#define CAN_RTL_VERSION     0x70u
#define CAN_NBTP            0x100u
#define CAN_DBTP            0x104u
#define CAN_TDC             0x108u
#define CAN_TSCC            0x10cu
#define CAN_RXFC            0x118u
#define CAN_IDCODE0         0x120u
#define CAN_IDMASK0         0x124u
#define CAN_TXFIC           0x200u
#define CAN_TXID            0x204u
#define CAN_TXDAT0          0x208u    /* TXDAT0..15: 16 words, 64 bytes */
#define CAN_RXFRD           0x400u

/* CAN_RTL_VERSION fields */
#define RTL_VER_MAJOR_SHIFT 4
#define RTL_VER_MAJOR_MASK  (0xfu << RTL_VER_MAJOR_SHIFT)
#define RTL_VER_MINOR_MASK  0xfu

/* CAN_MODE bits */
#define MODE_FDOE           (1u << 15)
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

/* CAN_RXFC bits */
#define FIFO_ENABLE         (1u << 0)
#define RX_FIFO_CNT_SHIFT   4
#define RX_FIFO_CNT_MASK    (0x7u << RX_FIFO_CNT_SHIFT)

/* TX/RX frame-info bits (FIC word) */
#define FORMAT_SHIFT        7
#define FORMAT_MASK         (1u << FORMAT_SHIFT)    /* 1 = extended id */
#define RTR_SHIFT           6
#define RTR_MASK            (1u << RTR_SHIFT)
#define FDF_MASK            (1u << 5)               /* CAN FD frame */
#define BRS_MASK            (1u << 4)               /* bit rate switch */
#define DLC_MASK            0xfu

/* NBTP fields (classic/arbitration bit timing) */
#define NBTP_NSJW_SHIFT     24
#define NBTP_NBRP_SHIFT     16
#define NBTP_NTSEG2_SHIFT   8
#define NBTP_NTSEG1_SHIFT   0

/* DBTP fields (FD data-phase bit timing) - narrower than NBTP:
 * brp 2..512 (stored (brp/2)-1 in 8 bits), tseg1 1..32, tseg2 1..16,
 * sjw 1..16 */
#define DBTP_DSJW_SHIFT     17
#define DBTP_DBRP_SHIFT     9
#define DBTP_DTSEG2_SHIFT   5
#define DBTP_DTSEG1_SHIFT   0

/* TDC fields (transmit delay compensation): TDC_OFFSET[6:1] stores
 * offset<<1, TDC_ENABLE bit0. a useful offset for the secondary sample
 * point is two thirds of the data bit time, i.e. clk/dbitrate*2/3. */
#define TDC_OFFSET_SHIFT    1
#define TDC_OFFSET_MAX      63u
#define TDC_ENABLE          (1u << 0)

/* acceptance filter width (29-bit id space) */
#define CAN_RX_FILTER_MASK  0x1fffffffu

/* loopback duplicate window in 100MHz timestamp ticks: the two fifo
 * copies of a self frame land back-to-back, a legitimate identical
 * frame cannot arrive this fast (len=0 @ 1Mbps takes ~47us). */
#define CAN_DUP_TS_WINDOW   2500u

/* ------------------------------------------------------------------ */
/* static state (contract: no dynamic allocation)                      */
/* ------------------------------------------------------------------ */

/* local device lifecycle (no shared state with the master) */
#define CAN_STATE_OFFLINE      0u      /* BSS zero: never init'ed */
#define CAN_STATE_READY        1u      /* init'ed, on the soft reset mode */
#define CAN_STATE_RUNNING      2u      /* on the bus */
#define CAN_STATE_STOPPED      3u      /* stopped: config kept, re-Init allowed */

/* ready queue: lock-free SPSC ring of frame copies. producer = the ISR
 * (drain), consumer = the task (CanRecv); both on CPU3, head written
 * only by the consumer, tail only by the producer (see net_dwmac.c for
 * the race this discipline prevents). capacity CAN_READY_Q_NUM-1. */
#define CAN_READY_Q_NUM     32u

/* per-controller hardware profile (immutable, from the profile headers). */
struct canPort {
    uint32_t portId;                   /* CAN_PORT_*_RK3568 (can.h) */
    uintptr_t base;                     /* controller MMIO base */
    uint32_t intid;                     /* GIC INTID owned by the slave */
    uint32_t irqPrio;                  /* LOS hwi priority */
    uint32_t baudclkHz;                /* cLK_CANx rate (master retarget) */
    uint32_t rxFifoDepth;             /* hardware rx fifo depth */
    uint32_t defaultBitrate;           /* when cfg->bitrate == 0 */
};

static const struct canPort g_ports[] = {
    { CAN_PORT_CAN1_RK3568, CAN1_BASE, CAN1_IRQ_INTID, CAN1_IRQ_LOS_PRIO,
      CAN1_BAUDCLK_HZ, CAN1_RX_FIFO_DEPTH, CAN1_DEFAULT_BITRATE },
};
#define CAN_PORT_SLOTS  ((uint32_t)(sizeof(g_ports) / sizeof(g_ports[0])))

/* per-port device: ALL runtime state is instance-scoped so two ports
 * can run concurrently without any shared mutable file-scope data */
struct canDevice {
    uint32_t state;                     /* CAN_STATE_* (local view) */
    uint32_t portId;                   /* CAN_PORT_* this instance serves */
    uint32_t mode;                      /* CAN_MODE_* (runtime) */
    uint32_t workMode;                 /* CAN_WORK_* (runtime) */
    uint32_t fdEnabled;                /* FD data path configured (DBTP+TDC) */
    uint32_t busOffCount;
    const struct canPort *port;        /* hardware profile (g_ports[slot]) */
    canStats stats;
    canFrame rdyQ[CAN_READY_Q_NUM];    /* per-instance ready ring */
    volatile uint32_t rdyHead, rdyTail;
    /* loopback duplicate suppression: LBACK+RXSTX writes the self frame
     * into the rx fifo twice with an identical ts word, so an exact ts
     * match right after a pop is the duplicate copy. */
    canFrame prevRx;
    uint32_t prevRxTs;
    uint32_t prevRxValid;
    uint32_t diagDupmiss;              /* dup-miss diagnostic limiter */
};

static struct canDevice g_devs[CAN_PORT_SLOTS];    /* zero = OFFLINE */

/* map a canDevice* back to its slot (rejects foreign pointers) */
static int CanDevSlot(const canDevice *dev)
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
/* register helpers (PA == VA identity mapping on the slave)           */
/* ------------------------------------------------------------------ */

static inline uint32_t CanRegRead(const struct canDevice *d, uint32_t off)
{
    return *(volatile uint32_t *)(d->port->base + off);
}

static inline void CanRegWrite(const struct canDevice *d, uint32_t off,
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
 * backend and net_dwmac.c call them the same way). the gic-v3 port's
 * createIrq applies the per-core ownership contract for sPIs. */
/* ------------------------------------------------------------------ */

extern UINT32 ArchHwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio,
                            HWI_MODE_T hwiMode, HWI_PROC_FUNC hwiHandler,
                            HwiIrqParam *irqParam);
extern UINT32 ArchHwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam);

/* ------------------------------------------------------------------ */
/* bit timing solver                                                   */
/*                                                                     */
/* bitrate = baudclk / (brp * (1 + ts1 + ts2)), brp even 2..512 (the    */
/* register stores (brp/2 - 1) - brp_inc = 2), ts1/ts2 1..128. the      */
/* solver walks brp upward (small divisors first = finer tq resolution) */
/* and targets the ciA sample point 87.5% (ts1 = n*7/8).                */
/* ------------------------------------------------------------------ */

static int CanCalcTiming(uint32_t baudclkHz, uint32_t bitrate,
                           canBittiming *out)
{
    uint32_t div;

    if ((baudclkHz == 0) || (bitrate == 0)) {
        return -1;
    }
    for (div = 2; div <= 512; div += 2) {
        uint32_t prod = div * bitrate;
        uint32_t n, ts1, ts2;

        if (prod == 0) {
            return -1;                 /* overflow guard (cannot happen) */
        }
        if ((baudclkHz % prod) != 0) {
            continue;                  /* no exact solution at this brp */
        }
        n = baudclkHz / prod;         /* tq per bit = 1 + ts1 + ts2 */
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

/* write NBTP from solved/raw timing. register encodings: brp stores
 * brp/2-1, segments store value-1. */
static int CanApplyTiming(const struct canDevice *d, const canBittiming *t)
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

/* ------------------------------------------------------------------ */
/* CAN FD data phase: len<->DLC mapping, DBTP solver/apply, TDC        */
/* ------------------------------------------------------------------ */

/* FD payload length <-> 4-bit DLC code (ISO 11898-1). non-standard
 * lengths 9..11/13..15/17..19/... are not legal on the wire and
 * rejected at send() time. */
static const uint8_t g_fdDlc2Len[16] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
};

static int CanFdLen2Dlc(uint32_t len)
{
    int i;

    for (i = 15; i >= 0; i--) {
        if (len == g_fdDlc2Len[i]) {
            return i;
        }
    }
    return -1;                          /* not a legal FD length */
}

/* solve the DATA-phase timing for DBTP: same algorithm as the nominal
 * solver but with the narrower DBTP field ranges (ts1 1..32, ts2 1..16)
 * and the ciA data-phase sample point target of 80%. */
static int CanCalcDataTiming(uint32_t baudclkHz, uint32_t bitrate,
                                canBittiming *out)
{
    uint32_t div;

    if ((baudclkHz == 0) || (bitrate == 0)) {
        return -1;
    }
    for (div = 2; div <= 512; div += 2) {
        uint32_t prod = div * bitrate;
        uint32_t n, ts1, ts2;

        if (prod == 0) {
            return -1;
        }
        if ((baudclkHz % prod) != 0) {
            continue;
        }
        n = baudclkHz / prod;         /* tq per bit = 1 + ts1 + ts2 */
        if ((n < 8) || (n > 49)) {     /* 1+32+16 = 49 tq max */
            continue;
        }
        ts1 = (n * 4u) / 5u;           /* sample point ~80% */
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
    return -1;
}

/* write DBTP (FD data-phase bit timing). narrower field ranges than
 * NBTP; same (brp/2)-1 / value-1 encodings. */
static int CanApplyDataTiming(const struct canDevice *d,
                                 const canBittiming *t)
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
    return 0;
}

/* FD transmit delay compensation: fast data phases (>1M) need TDC; the
 * offset is 2/3 of the data bit time. loopback modes keep TDC OFF - the
 * internal loopback has no transceiver delay and a TDC offset pushes the
 * secondary sample point past the signal, so BRS frames fail CRC and TX
 * hangs (measured: lb-fd-brs-2m, every frame -110). */
static void CanApplyTdc(const struct canDevice *d, uint32_t baudclkHz,
                          uint32_t dataBitrate, uint32_t workMode)
{
    uint32_t tdco, reg;

    if (dataBitrate == 0) {
        return;
    }
    if ((workMode == CAN_WORK_LOOPBACK) ||
        (workMode == CAN_WORK_SILENT_LB)) {
        CanRegWrite(d, CAN_TDC, 0);
        return;
    }
    tdco = (baudclkHz / dataBitrate) * 2u / 3u;
    if (tdco > TDC_OFFSET_MAX) {
        tdco = TDC_OFFSET_MAX;
    }
    reg = (tdco << TDC_OFFSET_SHIFT) | TDC_ENABLE;
    CanRegWrite(d, CAN_TDC, reg);
}

/* ------------------------------------------------------------------ */
/* RX fifo drain (ISR / recv in POLL+HYBRID)                           */
/* ------------------------------------------------------------------ */

/* pop one frame from the hardware fifo into the ready queue.
 * returns 1 when a frame was queued, 0 when the fifo is empty and
 * -1 on a dropped/invalid frame. */
static int CanRxPopOne(struct canDevice *d)
{
    uint32_t ficWord = CanRegRead(d, CAN_RXFRD);
    uint32_t idWord = CanRegRead(d, CAN_RXFRD);
    uint32_t ts = CanRegRead(d, CAN_RXFRD);
    uint32_t data[16];
    uint32_t isFd;
    canFrame f;
    uint32_t i;

    /* the fifo pointer advances per 32-bit read: drain the FULL frame
     * (fic + id + ts + 16 data words = 19 reads) - a classic frame
     * carries 2, an FD frame up to 16 data words, the element size is
     * fixed at 19. */
    for (i = 0; i < 16; i++) {
        data[i] = CanRegRead(d, CAN_RXFRD);
    }

    /* V1 silicon may surface an empty frame - skip it */
    if ((ficWord == 0) && (idWord == 0)) {
        return 0;
    }

    isFd = (ficWord & FDF_MASK) != 0;
    if (isFd) {
        /* FD frame on a classic-only port: drop it (the frame cannot
         * be represented in the contract in use) */
        if (!d->fdEnabled) {
            d->stats.rxDropped++;
            return -1;
        }
    }

    f.id = idWord & CAN_RX_FILTER_MASK;
    f.flags = 0;
    if ((ficWord & FORMAT_MASK) != 0) {
        f.flags |= CAN_FRAME_EFF;
    }
    if (isFd) {
        f.flags |= CAN_FRAME_FDF;
        if ((ficWord & BRS_MASK) != 0) {
            f.flags |= CAN_FRAME_BRS;
        }
    } else if ((ficWord & RTR_MASK) != 0) {
        f.flags |= CAN_FRAME_RTR;
    }
    f.reserved = 0;
    if (isFd) {
        f.len = g_fdDlc2Len[ficWord & DLC_MASK];  /* 9..15 -> 12..64 */
    } else {
        f.len = ficWord & DLC_MASK;                /* classic 0..8 */
        if (f.len > 8) {                            /* classic DLC 9..15 = reserved */
            d->stats.rxErrors++;
            return -1;
        }
    }
    for (i = 0; i < f.len; i++) {
        f.data[i] = (uint8_t)(data[i / 4] >> ((i % 4) * 8));
    }

    /* loopback duplicate: the second fifo copy of the same self frame,
     * written within CAN_DUP_TS_WINDOW of the first (RTR echoes carry
     * no data - compare the header only) */
    if (d->prevRxValid &&
        ((uint32_t)(ts - d->prevRxTs) <= CAN_DUP_TS_WINDOW) &&
        (f.id == d->prevRx.id) && (f.flags == d->prevRx.flags) &&
        (f.len == d->prevRx.len) &&
        (((f.flags & CAN_FRAME_RTR) != 0) ||
         (memcmp(f.data, d->prevRx.data, f.len) == 0))) {
        d->stats.rxDupSuppressed++;
        return 0;                       /* consumed, not delivered */
    }
    /* same frame again OUTSIDE the window: not the duplicate copy -
     * surfaced for bring-up (should never happen in the demo) */
    if ((d->diagDupmiss < 40) && d->prevRxValid &&
        (f.id == d->prevRx.id) && (f.flags == d->prevRx.flags) &&
        (f.len == d->prevRx.len) && ((uint32_t)(ts - d->prevRxTs) > CAN_DUP_TS_WINDOW)) {
        d->diagDupmiss++;
        PRINT_ERR("[can][diag] dup-miss: id=%x ts=%08x prev=%08x\n",
                  f.id, ts, d->prevRxTs);
    }
    d->prevRx = f;
    d->prevRxTs = ts;
    d->prevRxValid = 1;

    if (((d->rdyTail + 1u) % CAN_READY_Q_NUM) == d->rdyHead) {
        d->stats.rxOverflow++;         /* queue full: drop */
        d->stats.rxDropped++;
        return -1;
    }
    d->rdyQ[d->rdyTail] = f;
    CanDsb();                          /* frame visible before tail moves */
    d->rdyTail = (d->rdyTail + 1u) % CAN_READY_Q_NUM;
    return 1;
}

/* ~20us settle spin: the RXFC fill counter lags the pop (erratum 5),
 * so the drain loop re-reads it only after this window. ~30 loop
 * iterations per microsecond on a ~1.5GHz A55. */
static void CanSettleDelay(void)
{
    volatile uint32_t spin;

    for (spin = 0; spin < 600u; spin++) {
        __asm__ volatile("nop");
    }
}

/* drain everything the hardware fifo holds (depth 6, cnt is 3-bit).
 * one settle spin between pops: re-reading RXFC immediately after a
 * pop can still show the pre-pop count and yield a ghost element. */
static void CanRxDrain(struct canDevice *d)
{
    uint32_t pass = 0;

    while (pass < 2 * d->port->rxFifoDepth + 2) {
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
/* interrupt service (interrupt mode)                                  */
/*                                                                     */
/* ArchHwiCreate takes a plain VOID(VOID) entry, so each port gets a   */
/* tiny trampoline into the shared body below (g_isr_tab).            */
/* ------------------------------------------------------------------ */

static VOID CanIsrCommon(struct canDevice *d)
{
    uint32_t isr = CanRegRead(d, CAN_INT);

    /* W1C on the bits we handle */
    CanRegWrite(d, CAN_INT, isr & CAN_INT_ENABLED);

    d->stats.irqCount++;

    if ((isr & RX_FINISH_INT) != 0) {
        CanRxDrain(d);
    }
    if ((isr & (BUS_OFF_INT | RX_FIFO_OV_INT | RX_BUF_OV_INT)) != 0) {
        if ((isr & BUS_OFF_INT) != 0) {
            d->busOffCount++;
        }
        d->stats.rxErrors++;
    }
    if ((isr & TX_LOSTARB_INT) != 0) {
        d->stats.txErrors++;
    }
    /* TX_FINISH: CanSend is synchronous and clears its own completion;
     * nothing to do here (the W1C above releases the irq line). */
}

static VOID Can1Isr(VOID)
{
    CanIsrCommon(&g_devs[0]);
}

/* slot-indexed ISR entry table (CanStart) */
static const HWI_PROC_FUNC g_isrTab[CAN_PORT_SLOTS] = { Can1Isr };

/* ------------------------------------------------------------------ */
/* contract implementation                                            */
/* ------------------------------------------------------------------ */

int CanInit(const canConfig *cfg, canDevice **dev)
{
    canBittiming timing;
    canBittiming dataTiming;
    uint32_t dataBitrate = 0;
    uint32_t modeVal;
    const struct canPort *port;
    struct canDevice *d;
    uint32_t slot;
    int i;

    if ((cfg == NULL) || (dev == NULL)) {
        return CAN_EINVAL;
    }
    if ((cfg->mode > CAN_MODE_HYBRID) ||
        (cfg->workMode > CAN_WORK_SILENT_LB)) {
        return CAN_EINVAL;
    }

    /* --- port lookup: cfg->port_id selects the controller --- */
    port = NULL;
    for (slot = 0; slot < CAN_PORT_SLOTS; slot++) {
        if (g_ports[slot].portId == cfg->portId) {
            port = &g_ports[slot];
            break;
        }
    }
    if (port == NULL) {
        return CAN_ENODEV;
    }
    d = &g_devs[slot];
    if ((d->state == CAN_STATE_RUNNING) || (d->state == CAN_STATE_READY)) {
        return CAN_EBUSY;               /* held: stop first (STOPPED may re-init) */
    }
    /* bind the profile BEFORE the first MMIO below: the register
     * helpers route through d->port->base and CanInit's early code
     * paths (soft reset, filters, fifo) run long before the tail of
     * this function assigns the rest of the device state */
    d->port = port;

    /* PRECONDITION (not checked at runtime): the master mcs_can.ko is
     * loaded, so the port clocks are on and the resets are released
     * (see board/DRIVER_GUIDE.md). with the clocks gated the first
     * MMIO below stalls the AXI bus and hangs the core. */

    /* --- bit timing: raw wins, else solve from the profile baudclk --- */
    if (cfg->useRawTiming != 0) {
        timing = cfg->timing;
    } else {
        uint32_t bitrate = cfg->bitrate;
        if (bitrate == 0) {
            bitrate = port->defaultBitrate;
        }
        if (CanCalcTiming(port->baudclkHz, bitrate, &timing) != 0) {
            PRINT_ERR("[can] no bit timing: %u bps @ %u Hz "
                      "(800k is unreachable at 100MHz)\n",
                      bitrate, port->baudclkHz);
            return CAN_EINVAL;
        }
    }

    /* --- CAN FD data phase: solve/validate up front so a bad rate
     *     fails BEFORE any register is touched. data-phase timing shares
     *     the use_raw_timing style selection with the nominal phase. --- */
    if (cfg->fdEnable != 0) {
        if (cfg->useRawTiming != 0) {
            dataTiming = cfg->dataTiming;
            dataBitrate = port->baudclkHz /
                           (dataTiming.brp * (1 + dataTiming.ts1 +
                                               dataTiming.ts2));
        } else {
            dataBitrate = cfg->dataBitrate;
            if (dataBitrate == 0) {
                dataBitrate = CAN_DEFAULT_DATA_BITRATE;
            }
            if (CanCalcDataTiming(port->baudclkHz, dataBitrate,
                                     &dataTiming) != 0) {
                PRINT_ERR("[can] no FD data timing: %u bps @ %u Hz "
                          "(dbtp tseg1<=32 tseg2<=16)\n",
                          dataBitrate, port->baudclkHz);
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
    /* HW mask semantics: bit 1 = don't care, bit 0 = compare. The
     * config filterMask uses the software convention 1 = compare,
     * so program the complement. */
    if (cfg->filterEn != 0) {
        CanRegWrite(d, CAN_IDCODE, cfg->filterCode & CAN_RX_FILTER_MASK);
        CanRegWrite(d, CAN_IDMASK, CAN_RX_FILTER_MASK & ~(cfg->filterMask));
        /* close the 5 auxiliary banks (mask 0 = compare every bit,
         * code 0 matches only id 0) so frames fall through to the
         * global filter */
        for (i = 0; i < 5; i++) {
            CanRegWrite(d, CAN_IDCODE0 + (uint32_t)i * 8, 0);
            CanRegWrite(d, CAN_IDMASK0 + (uint32_t)i * 8, 0);
        }
    } else {
        CanRegWrite(d, CAN_IDCODE, 0);
        CanRegWrite(d, CAN_IDMASK, CAN_RX_FILTER_MASK);
        /* the 5 auxiliary banks stay all-open */
        for (i = 0; i < 5; i++) {
            CanRegWrite(d, CAN_IDCODE0 + (uint32_t)i * 8, 0);
            CanRegWrite(d, CAN_IDMASK0 + (uint32_t)i * 8, CAN_RX_FILTER_MASK);
        }
    }

    /* --- rx fifo enable + timestamp counter enable: every rx fifo
     *     element carries a ts word --- */
    CanRegWrite(d, CAN_RXFC, CanRegRead(d, CAN_RXFC) | FIFO_ENABLE);
    CanRegWrite(d, CAN_TSCC, CanRegRead(d, CAN_TSCC) | TSC_ENABLE);

    /* --- mode bits: FDOE (required even for classic-only), auto-retransmit,
     *     RXSTX + COVER, then the work mode.
     *     LOOPBACK silicon facts: no LBACK = no self-ack (TX hangs -110 on
     *     a dead bus); RXSTX is the only path a self frame takes into the
     *     rx fifo; LBACK+RXSTX delivers every frame as TWO fifo elements
     *     (the second is suppressed by ts proximity, CAN_DUP_TS_WINDOW). */
    modeVal = MODE_FDOE | MODE_AUTO_RETX | MODE_RXSTX | MODE_COVER;
    switch (cfg->workMode) {
    case CAN_WORK_NORMAL:
        break;
    case CAN_WORK_LOOPBACK:
    case CAN_WORK_SILENT_LB:
        modeVal |= MODE_SELF_TEST | MODE_LBACK | MODE_SILENT;
        break;
    case CAN_WORK_SILENT:
        modeVal |= MODE_SILENT;
        break;
    default:
        return CAN_EINVAL;
    }
    CanRegWrite(d, CAN_MODE, modeVal);

    /* --- bit timing (valid in reset mode only) --- */
    if (CanApplyTiming(d, &timing) != 0) {
        PRINT_ERR("[can] invalid raw timing Brp=%u Sjw=%u Ts1=%u Ts2=%u\n",
                  timing.brp, timing.sjw, timing.ts1, timing.ts2);
        return CAN_EINVAL;
    }

    /* --- CAN FD data phase (reset mode only). the IP RTL version is
     *     printed once per init: rk3568 v2/v3 silicon implements the FD
     *     data path but carries receive errata for specific FD frame
     *     patterns - keep it visible for field diagnosis. --- */
    if (cfg->fdEnable != 0) {
        uint32_t rtl = CanRegRead(d, CAN_RTL_VERSION);

        if (CanApplyDataTiming(d, &dataTiming) != 0) {
            PRINT_ERR("[can] invalid raw data timing Brp=%u Sjw=%u Ts1=%u "
                      "Ts2=%u (dbtp: tseg1<=32 tseg2<=16 Sjw<=16)\n",
                      dataTiming.brp, dataTiming.sjw,
                      dataTiming.ts1, dataTiming.ts2);
            return CAN_EINVAL;
        }
        CanApplyTdc(d, port->baudclkHz, dataBitrate, cfg->workMode);
        PRINT_INFO("[can] CAN%u IP rtl v%u.%u, FD data phase %u bps\n",
                   port->portId,
                   (rtl & RTL_VER_MAJOR_MASK) >> RTL_VER_MAJOR_SHIFT,
                   rtl & RTL_VER_MINOR_MASK, dataBitrate);
    } else {
        uint32_t rtl = CanRegRead(d, CAN_RTL_VERSION);

        PRINT_INFO("[can] CAN%u IP rtl v%u.%u, classic CAN 2.0B\n",
                   port->portId,
                   (rtl & RTL_VER_MAJOR_MASK) >> RTL_VER_MAJOR_SHIFT,
                   rtl & RTL_VER_MINOR_MASK);
    }

    /* --- software state --- */
    (VOID)memset(&d->stats, 0, sizeof(d->stats));
    d->busOffCount = 0;
    d->rdyHead = 0;
    d->rdyTail = 0;
    d->prevRxValid = 0;
    d->diagDupmiss = 0;
    d->fdEnabled = (cfg->fdEnable != 0);
    d->portId = cfg->portId;
    d->mode = cfg->mode;
    d->workMode = cfg->workMode;
    d->port = port;
    d->state = CAN_STATE_READY;

    *dev = d;
    return 0;
}

int CanStart(canDevice *dev)
{
    int slot = CanDevSlot(dev);

    if (slot < 0) {
        return CAN_EINVAL;
    }
    if (dev->state == CAN_STATE_RUNNING) {
        return 0;                       /* idempotent */
    }
    if ((dev->state != CAN_STATE_READY) && (dev->state != CAN_STATE_STOPPED)) {
        return CAN_EIO;                 /* FAULT: needs init first */
    }

    if (dev->mode != CAN_MODE_POLL) {
        if (ArchHwiCreate((HWI_HANDLE_T)dev->port->intid,
                          (HWI_PRIOR_T)dev->port->irqPrio,
                          HWI_MODE_TRIGGER_LEVEL,
                          g_isrTab[slot], (HwiIrqParam *)0) != LOS_OK) {
            PRINT_ERR("[can] ArchHwiCreate(%u) failed\n", dev->port->intid);
            return CAN_EIO;
        }
    }

    /* clear anything latched, then unmask rx/tx/errors (POLL keeps the
     * line silent - recv() drives everything) */
    CanRegWrite(dev, CAN_INT, 0xffffu);
    if (dev->mode != CAN_MODE_POLL) {
        CanRegWrite(dev, CAN_INT_MASK, ~CAN_INT_ENABLED);
    } else {
        CanRegWrite(dev, CAN_INT_MASK, 0xffffu);
    }

    /* leave the soft reset mode: the controller joins the bus */
    CanRegWrite(dev, CAN_MODE, CanRegRead(dev, CAN_MODE) | WORK_MODE);

    dev->state = CAN_STATE_RUNNING;
    return 0;
}

int CanStop(canDevice *dev, uint32_t timeoutUs)
{
    if (CanDevSlot(dev) < 0) {
        return CAN_EINVAL;
    }
    if (dev->state != CAN_STATE_RUNNING) {
        /* never started (READY) or already stopped: nothing to
         * quiesce, but a READY port is released for a later re-Init */
        if (dev->state == CAN_STATE_READY) {
            dev->state = CAN_STATE_STOPPED;
        }
        return 0;
    }
    (VOID)timeoutUs;                   /* no DMA to quiesce: the reset
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

    dev->rdyHead = 0;
    dev->rdyTail = 0;

    dev->state = CAN_STATE_STOPPED;
    return 0;
}

int CanSend(canDevice *dev, const canFrame *frame, uint32_t timeoutUs)
{
    uint32_t id, fic, data[16];
    uint32_t budget, dlc;
    int isEff, isRtr, isFd, isBrs, i;

    if ((CanDevSlot(dev) < 0) || (frame == NULL)) {
        return CAN_EINVAL;
    }
    if (dev->state != CAN_STATE_RUNNING) {
        return CAN_EIO;
    }

    isEff = (frame->flags & CAN_FRAME_EFF) != 0;
    isRtr = (frame->flags & CAN_FRAME_RTR) != 0;
    isFd = (frame->flags & CAN_FRAME_FDF) != 0;
    isBrs = (frame->flags & CAN_FRAME_BRS) != 0;

    /* FD frames need an fd_enable'ed port; RTR is classic-only; BRS
     * requires FDF; classic payloads are 0..8, FD payloads must be one
     * of the legal DLC-mapped lengths (12/16/20/24/32/48/64). */
    if (isFd) {
        if (!dev->fdEnabled) {
            return CAN_ENOTSUP;
        }
        if (isRtr) {
            return CAN_EINVAL;
        }
        if (CanFdLen2Dlc(frame->len) < 0) {
            return CAN_EMSGSIZE;
        }
    } else {
        if (frame->len > 8) {
            return CAN_EMSGSIZE;
        }
    }
    if (isEff) {
        if (frame->id > 0x1fffffffu) {
            return CAN_EINVAL;
        }
    } else {
        if (frame->id > 0x7ffu) {
            return CAN_EINVAL;
        }
    }

    /* tx fifo is ONE frame deep: wait for any in-flight frame first.
     * ~30 loop iterations per microsecond on a ~1.5GHz A55. */
    budget = (timeoutUs == 0) ? 1u : (timeoutUs * 30u);
    while ((CanRegRead(dev, CAN_CMD) & CAN_TX0_REQ) != 0) {
        if (budget-- == 0) {
            dev->stats.txTimeouts++;
            return CAN_ETIMEDOUT;
        }
    }

    /* frame assembly: FD payload lengths map to the 4-bit DLC code
     * (12->9 .. 64->15), classic lengths use the plain byte count. */
    if (isFd) {
        int code = CanFdLen2Dlc(frame->len);

        dlc = (uint32_t)code;
    } else {
        dlc = frame->len;
    }
    id = frame->id;
    fic = dlc & DLC_MASK;
    if (isEff) {
        fic |= FORMAT_MASK;
    }
    if (isFd) {
        fic |= FDF_MASK;
        if (isBrs) {
            fic |= BRS_MASK;
        }
    }
    if (isRtr) {
        fic |= RTR_MASK;
    }
    for (i = 0; i < 16; i++) {              /* 16 data words, zero pad */
        uint32_t w = 0;
        int j;

        for (j = 0; j < 4; j++) {
            uint32_t off = (uint32_t)i * 4u + (uint32_t)j;

            if (off < (uint32_t)frame->len) {
                w |= (uint32_t)frame->data[off] << (j * 8);
            }
        }
        data[i] = w;
    }
    CanRegWrite(dev, CAN_TXID, id);
    CanRegWrite(dev, CAN_TXFIC, fic);
    for (i = 0; i < 16; i++) {
        CanRegWrite(dev, CAN_TXDAT0 + (uint32_t)i * 4u, data[i]);
    }
    CanDsb();

    /* launch with the SPACE_RX handshake: set MODE_SPACE_RX, raise
     * TX0_REQ, clear MODE_SPACE_RX. MODE_RXSTX is latched permanently
     * in CanInit - raising it only around the command misses the echo. */
    CanRegWrite(dev, CAN_MODE, CanRegRead(dev, CAN_MODE) | MODE_SPACE_RX);
    CanRegWrite(dev, CAN_CMD, CAN_TX0_REQ);
    CanRegWrite(dev, CAN_MODE, CanRegRead(dev, CAN_MODE) & ~MODE_SPACE_RX);
    dev->stats.txAccepted++;

    /* bounded wait for THIS frame to complete (TX0_REQ self-clears) */
    budget = (timeoutUs == 0) ? 1u : (timeoutUs * 30u);
    while ((CanRegRead(dev, CAN_CMD) & CAN_TX0_REQ) != 0) {
        if (budget-- == 0) {
            dev->stats.txTimeouts++;
            return CAN_ETIMEDOUT;
        }
    }
    /* consume the TX_FINISH status (W1C) so it cannot stack up */
    CanRegWrite(dev, CAN_INT, TX_FINISH_INT);

    dev->stats.txCompleted++;
    return 0;
}

int CanRecv(canDevice *dev, canFrame *frame)
{
    if ((CanDevSlot(dev) < 0) || (frame == NULL)) {
        return CAN_EINVAL;
    }
    if (dev->state != CAN_STATE_RUNNING) {
        return CAN_EIO;
    }

    /* POLL: recv() is the only reaper. HYBRID: the ISR reaps ahead of
     * us; reaping here too is idempotent and backstops a dropped IRQ. */
    if (dev->mode != CAN_MODE_IRQ) {
        CanRxDrain(dev);
    }

    while (dev->rdyHead != dev->rdyTail) {
        *frame = dev->rdyQ[dev->rdyHead];
        dev->rdyHead = (dev->rdyHead + 1u) % CAN_READY_Q_NUM;
        dev->stats.rxDelivered++;
        return 0;
    }
    return CAN_EAGAIN;
}

int CanGetBusState(canDevice *dev, canBusInfo *info)
{
    uint32_t rxerr, txerr, stateReg;

    if ((CanDevSlot(dev) < 0) || (info == NULL)) {
        return CAN_EINVAL;
    }

    if (dev->state != CAN_STATE_RUNNING) {
        info->state = CAN_BUS_STOPPED;
        info->rxErrCnt = 0;
        info->txErrCnt = 0;
        info->busOffCount = dev->busOffCount;
        return 0;
    }

    rxerr = CanRegRead(dev, CAN_RX_ERR_CNT) & 0xffu;
    txerr = CanRegRead(dev, CAN_TX_ERR_CNT) & 0xffu;
    stateReg = CanRegRead(dev, CAN_STATE_REG);

    if ((stateReg & CAN_STATE_BUSOFF) != 0) {
        info->state = CAN_BUS_BUS_OFF;
    } else if ((rxerr >= 128) || (txerr >= 128)) {
        info->state = CAN_BUS_ERROR_PASSIVE;
    } else if ((rxerr >= 96) || (txerr >= 96)) {
        info->state = CAN_BUS_ERROR_WARNING;
    } else {
        info->state = CAN_BUS_ERROR_ACTIVE;
    }
    info->rxErrCnt = rxerr;
    info->txErrCnt = txerr;
    info->busOffCount = dev->busOffCount;
    return 0;
}

int CanGetStats(canDevice *dev, canStats *stats)
{
    if ((CanDevSlot(dev) < 0) || (stats == NULL)) {
        return CAN_EINVAL;
    }
    *stats = dev->stats;
    stats->busOffCount = dev->busOffCount;
    return 0;
}

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
 * net_dwmac.c - real-time NIC data plane for the AMP slave core on the
 * RK3588 GMAC1 (fe1c0000, RGMII, YT9214 switch = bq3588hm ETH2).
 * Protocol-agnostic: EtherCAT, proprietary or raw-Ethernet users all ride
 * the same frame-level contract (net.h, v3 Net* surface).
 *
 * Division of labor: the Linux master (mcs_net.ko) owns clocks, reset,
 * pinmux + php_grf interface select + sys_grf RGMII delays, PHY/MDIO and
 * link polling; this file owns the RX/TX data path and its interrupt
 * (LOS ArchHwiCreate configures the GICD for the owned SPI - group/
 * route/trigger/priority, per-core ownership):
 *
 *   NetInit   - validate config (port/mode), reset the DMA, program
 *                 MTL/DMA chan 0, set the rx filter, build the
 *                 descriptor rings (static, zero dynamic allocation).
 *                 Legal in READY = reconfigure; RUNNING must Stop first.
 *   NetStart  - mode-aware bring-up: IRQ/HYBRID register INTID 266 via
 *                 ArchHwiCreate (level-triggered; the gic-v3 port routes
 *                 the SPI to this core) and arm DMA interrupts; POLL
 *                 skips both (MAC silent on the wire, Recv() drives)
 *   NetSend   - copy in, own descriptor, ring doorbell (tail ptr);
 *                 TX completion is reaped lazily (no TIE)
 *   NetRecv   - dequeue from the ready queue, copy out, re-arm the
 *                 descriptor; in POLL/HYBRID it reaps the RX ring first
 *   NetStop   - quiesce within a bounded timeout (DMA_DEBUG_STATUS),
 *                 deregister the IRQ (interrupt modes), reset the rings
 *
 * Link policy: none on this port. The YT9214 behind the RGMII runs the
 * MAC side fixed 1000M full duplex (copper-side negotiation is the
 * switch's business), so GMAC_CONFIG is always GMII/1000M/full and
 * NetConfig.autoneg=0 returns -NET_ENOTSUP. An MDIO probe (PHYSID1)
 * is informational only: an unmanaged (dumb-strapped) YT9214 stays
 * silent on MDIO and the master publishes the fixed link.
 *
 * Bounded waits (MDIO busy, DMA soft reset, Stop quiesce) use the ARM
 * generic timer (CNTPCT_EL0 / CNTFRQ_EL0): the slave core is CPU7
 * (A76 @ ~2.2GHz) on RK3588 and the RK3568 port's "~30 iterations/us"
 * A55 calibration does not carry over.
 *
 * Memory model: the DMA pools live in the dedicated Normal Non-Cacheable
 * window at NET_DMA_BASE (mapped by los_mmu.c), so PA==VA holds and
 * the descriptors/buffers are DMA-coherent without cache maintenance
 * even with the slave D-cache enabled. NetInit explicitly (re)writes
 * every descriptor, so the pools do not rely on .bss zeroing. All ring
 * state is static BSS.
 */

#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "los_arch_interrupt.h"
#include "net.h"
#include "net_ctrl.h"
#include "gmac1_profile.h"

/* ------------------------------------------------------------------ */
/* Generic-timer helpers (bounded waits on any core frequency)          */
/* ------------------------------------------------------------------ */

static inline uint64_t NetCntpct(void)
{
    uint64_t v;

    /* CNTVCT, not CNTPCT: on this board a NS-EL1 read of the physical
     * counter freezes CPU7 outright (verified 2026-10-16: boot probe read
     * ver110=0x3051 fine, then the first "mrs cntpct_el0" never returned).
     * The virtual counter runs fine (the tick pump uses it); its rate is
     * the same CNTFRQ, so timeouts are unaffected. */
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
}

static inline uint32_t NetCntfrq(void)
{
    uint32_t v;

    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline uint64_t NetUsToTicks(uint32_t us)
{
    return ((uint64_t)NetCntfrq() * (uint64_t)us) / 1000000u;
}

static inline int NetTimeReached(uint64_t deadline)
{
    return (int64_t)(NetCntpct() - deadline) >= 0;
}

/* ------------------------------------------------------------------ */
/* Register map (dwmac, all offsets relative to GMAC1_BASE - same IP   */
/* generation as the RK3568 GMAC1: MAC 0x0000, MDIO 0x0200, MTL        */
/* 0x0c00, DMA 0x1000, chan0 0x1100)                                   */
/* ------------------------------------------------------------------ */

/* MAC */
#define GMAC_CONFIG             0x0000u
#define GMAC_PACKET_FILTER      0x0008u
#define GMAC_ADDR0_HIGH         0x0300u
#define GMAC_ADDR0_LOW          0x0304u

#define GMAC_CONFIG_IPC         (1u << 27)
#define GMAC_CONFIG_BE          (1u << 18)
#define GMAC_CONFIG_JD          (1u << 17)
#define GMAC_CONFIG_PS          (1u << 15)
#define GMAC_CONFIG_FES         (1u << 14)
#define GMAC_CONFIG_DM          (1u << 13)
#define GMAC_CONFIG_DCRS        (1u << 9)
#define GMAC_CONFIG_TE          (1u << 1)
#define GMAC_CONFIG_RE          (1u << 0)

#define GMAC_PACKET_FILTER_PR   (1u << 0)
#define GMAC_PACKET_FILTER_HMC  (1u << 2)
#define GMAC_PACKET_FILTER_PM   (1u << 4)
#define GMAC_PACKET_FILTER_PCF  (1u << 7)

/* MTL */
#define MTL_OPERATION_MODE      0x0c00u
#define MTL_RXQ_DMA_MAP0        0x0c30u
#define MTL_TXQ0_OP_MODE        0x0d00u
#define MTL_RXQ0_OP_MODE        0x0d30u

#define MTL_OP_SCHALG_SP        (0x3u << 5)
#define MTL_OP_RAA_WSP          (1u << 2)
#define MTL_OP_MODE_RSF         (1u << 5)
#define MTL_OP_MODE_TXQEN       (1u << 3)
#define MTL_OP_MODE_TSF         (1u << 1)
#define MTL_OP_MODE_TTC_32      0u
#define MTL_OP_MODE_RTC_64      0u

/* DMA */
#define DMA_BUS_MODE            0x1000u
#define DMA_SYS_BUS_MODE        0x1004u
#define DMA_DEBUG_STATUS_0      0x100cu

#define DMA_BUS_MODE_SFT_RESET  (1u << 0)

#define DMA_SYS_BUS_MODE_EAME   (1u << 11)
#define DMA_SYS_BUS_MODE_FB     (1u << 0)
#define DMA_SYS_BUS_BLEN4       (1u << 1)
#define DMA_SYS_BUS_BLEN8       (1u << 2)
#define DMA_SYS_BUS_BLEN16      (1u << 3)
#define DMA_SYS_BUS_BLEN32      (1u << 4)

/* DMA channel 0 register block (0x1100 + ch * 0x80, ch = 0 here) */
#define DMA_CHAN_TX_CONTROL     0x1104u
#define DMA_CHAN_RX_CONTROL     0x1108u
#define DMA_CHAN_TX_BASE        0x1114u
#define DMA_CHAN_RX_BASE        0x111cu
#define DMA_CHAN_TX_END_ADDR    0x1120u   /* TX tail pointer / doorbell */
#define DMA_CHAN_RX_END_ADDR    0x1128u   /* RX tail pointer */
#define DMA_CHAN_TX_RING_LEN    0x112cu
#define DMA_CHAN_RX_RING_LEN    0x1130u
#define DMA_CHAN_INTR_ENA       0x1134u
#define DMA_CHAN_STATUS         0x1160u

#define DMA_CONTROL_ST          (1u << 0)
#define DMA_CONTROL_SR          (1u << 0)
#define DMA_CONTROL_OSP         (1u << 4)
#define DMA_TXPBL_SHIFT         16
#define DMA_RBSZ_SHIFT          1
#define DMA_RBSZ_MASK           0x3fffu   /* GENMASK(14,1) */

#define DMA_CHAN_STATUS_TI      (1u << 0)
#define DMA_CHAN_STATUS_TBU     (1u << 2)
#define DMA_CHAN_STATUS_RI      (1u << 6)
#define DMA_CHAN_STATUS_RBU     (1u << 7)
#define DMA_CHAN_STATUS_FBE     (1u << 12)
#define DMA_CHAN_STATUS_CDE     (1u << 13)
#define DMA_CHAN_STATUS_AIS     (1u << 14)
#define DMA_CHAN_STATUS_NIS     (1u << 15)

#define DMA_CHAN_INTR_ENA_NIE   (1u << 16)
#define DMA_CHAN_INTR_ENA_AIE   (1u << 15)
#define DMA_CHAN_INTR_ENA_CDE   (1u << 13)
#define DMA_CHAN_INTR_ENA_FBE   (1u << 12)
#define DMA_CHAN_INTR_ENA_RBUE  (1u << 7)
#define DMA_CHAN_INTR_ENA_RIE   (1u << 6)

/* DMA_DEBUG_STATUS_0: TX state in [3:0], RX state in [7:4]; 0 = stopped */
#define DMA_DBG_STATE_IDLE      0xf0u    /* both TS and RS fields zero */

/* Enhanced (16-byte) descriptor words */
struct net_dma_desc {
    volatile uint32_t d0;   /* read: buffer 1 address */
    volatile uint32_t d1;   /* read: buffer 2 address (unused) */
    volatile uint32_t d2;   /* TX read: buffer 1 size; RX write-back: RDES1 */
    volatile uint32_t d3;   /* read: OWN/FD/LD/...; write-back status */
};

#define TDES2_B1SIZE_MASK       0x3fffu
#define TDES3_PACKET_SIZE_MASK  0x7fffu
#define TDES3_LD                (1u << 28)
#define TDES3_FD                (1u << 29)
#define TDES3_IOC               (1u << 30)
#define TDES3_OWN               (1u << 31)

#define RDES3_PACKET_SIZE_MASK  0x7fffu
#define RDES3_ERROR_SUMMARY     (1u << 15)
#define RDES3_BUF1V             (1u << 24)
/* RDES3.IOC is bit20 (unlike TDES3.IOC=bit30): arming with bit30 set
 * marks the descriptor CTXT (context) and the DMA never writes frame
 * data into it - measured as a fully dead RX datapath on rk3588. */
#define RDES3_IOC               (1u << 20)
#define RDES3_LD                (1u << 28)
#define RDES3_FD                (1u << 29)
#define RDES3_CTXT              (1u << 30)
#define RDES3_OWN               (1u << 31)

/* ------------------------------------------------------------------ */
/* Static pools (contract: no dynamic allocation)                     */
/* ------------------------------------------------------------------ */

#define N_DESC      NET_DESC_NUM
#define FRAME_SIZE  NET_FRAME_SIZE

/* DMA pools inside the NC window (see gmac1_profile.h for the layout
 * contract). const pointers keep every existing g_rx_desc[i] /
 * g_tx_pool[i][j] / &g_rx_desc[N_DESC-1] expression source-compatible
 * with the old static-array form. */
static struct net_dma_desc *const g_rx_desc =
    (struct net_dma_desc *)(NET_DMA_BASE + NET_DMA_RX_DESC_OFF);
static struct net_dma_desc *const g_tx_desc =
    (struct net_dma_desc *)(NET_DMA_BASE + NET_DMA_TX_DESC_OFF);
static uint8_t (*const g_rx_pool)[FRAME_SIZE] =
    (uint8_t (*)[FRAME_SIZE])(NET_DMA_BASE + NET_DMA_RX_POOL_OFF);
static uint8_t (*const g_tx_pool)[FRAME_SIZE] =
    (uint8_t (*)[FRAME_SIZE])(NET_DMA_BASE + NET_DMA_TX_POOL_OFF);

/* per-slot software state, indexed the same as the descriptor rings */
#define SLOT_FREE     0u   /* desc armed with OWN, empty (RX) / reusable (TX) */
#define SLOT_READY    1u   /* RX: frame waiting in the ready queue */
#define SLOT_PENDING  2u   /* TX: owned by DMA (or completed, not yet reaped) */

static uint8_t g_rx_state[N_DESC];
static uint8_t g_tx_state[N_DESC];
static uint16_t g_rx_len[N_DESC];

/* RX ready queue: lock-free SPSC ring of slot indices.
 * Single producer = the ISR (NetRxReap), single consumer = the task
 * (NetRecv) - both run on CPU7, so there is no cross-core visibility
 * problem; head is written ONLY by the consumer, tail ONLY by the
 * producer, and no shared read-modify-write counter exists (a shared
 * g_rdy_cnt ++/-- raced once and corrupted the queue index, observed
 * as a slave data-abort hang 2026-09-27). Capacity N_DESC-1. */
static uint8_t g_rdy_q[N_DESC];
static volatile uint32_t g_rdy_head, g_rdy_tail;

/* cursor of the RX reap scan (next desc the ISR expects to be OWN) */
static uint32_t g_rx_cur;
/* TX allocation cursor: free slots are always a contiguous tail here */
static uint32_t g_tx_alloc;

struct NetDevice {
    uint32_t state;                     /* NET_STATE_* (slave-local view) */
    uint32_t port_id;                   /* NET_PORT_* this instance serves */
    uint32_t mode;                      /* NET_MODE_* (runtime, from config) */
    uint32_t promisc;                   /* 1=promiscuous rx filter */
    volatile struct net_ctrl *ctrl;  /* shared block @ 0x740ff000 */
    NetStats stats;
};

static struct NetDevice g_dev = {
    .state = NET_STATE_OFFLINE,
};

/* ------------------------------------------------------------------ */
/* Active instance parameters. Defaults are the compiled-in gmac1      */
/* profile; NetInit() overrides them from the ctrl v2 block when the   */
/* master handed over gmac0 (fe1b0000, RTL8211F) instead - the same    */
/* firmware then serves either port.                                   */
/* ------------------------------------------------------------------ */
static uintptr_t g_mac_base = GMAC1_BASE;
static uint32_t g_irq_intid = GMAC1_IRQ_INTID;
static uint32_t g_phy_addr = GMAC1_PHY_ADDR;

/* ------------------------------------------------------------------ */
/* Register helpers                                                   */
/* ------------------------------------------------------------------ */

static inline uint32_t NetRegRead(uint32_t off)
{
    return *(volatile uint32_t *)(g_mac_base + off);
}

static inline void NetRegWrite(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(g_mac_base + off) = val;
}

static inline void NetDsb(void)
{
    __asm__ volatile("dsb sy" ::: "memory");
}

/* ------------------------------------------------------------------ */
/* LOS interrupt API                                                  */
/*                                                                     */
/* ArchHwiCreate/ArchHwiDelete live in kernel/liteos_m/arch/arm/common/
 * los_common_interrupt.c; no public header declares them (the mica irq
 * backend calls them the same way). The gic-v3 port's createIrq applies
 * the per-core ownership contract for SPIs (Group1-NS, IROUTER to this
 * core, ICFGR from hwiMode, priority, enable) - no GICD work is needed
 * here, and the master never configures the slave's interrupts. */
/* ------------------------------------------------------------------ */

extern UINT32 ArchHwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio,
                            HWI_MODE_T hwiMode, HWI_PROC_FUNC hwiHandler,
                            HwiIrqParam *irqParam);
extern UINT32 ArchHwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam);

/* ------------------------------------------------------------------ */
/* MDIO (clause 22) read - PHY probe only                             */
/*                                                                     */
/* The slave NEVER writes MDIO on this port (no forced-link policy:    */
/* net.h), so the PHY state stays exactly as the master probed it.     */
/* The only use is the informational PHYSID1 probe in NetInit: an      */
/* unmanaged YT9214 (dumb-switch strapping from SPI flash) stays       */
/* silent and reads back 0xffff - tolerated, the master publishes the  */
/* fixed 1000M link.                                                   */
/*                                                                     */
/* MDIO_CR_VAL is carried over from the validated RK3568 port (same    */
/* dwmac MDIO block, pclk in the same range).                          */
/* ------------------------------------------------------------------ */

#define GMAC1_MDIO_ADDR_REG     0x0200u
#define GMAC1_MDIO_DATA_REG     0x0204u
#define MDIO_BUSY               (1u << 0)
#define MDIO_GOC_READ           (3u << 2)
#define MDIO_CR_VAL             5u      /* CSR range for a ~125MHz clock */
#define MDIO_PHY_SHIFT          21
#define MDIO_REG_SHIFT          16
#define MDIO_CR_SHIFT           8

#define MDIO_TIMEOUT_US         10000u  /* one MDIO frame is ~64 MDC
                                         * cycles; 10ms is a very
                                         * generous bound */

static int NetMdioRead(uint32_t phy, uint32_t reg, uint16_t *val)
{
    uint64_t deadline = NetCntpct() + NetUsToTicks(MDIO_TIMEOUT_US);
    uint32_t v;

    v = MDIO_BUSY | MDIO_GOC_READ |
        (phy << MDIO_PHY_SHIFT) | (reg << MDIO_REG_SHIFT) |
        (MDIO_CR_VAL << MDIO_CR_SHIFT);
    NetRegWrite(GMAC1_MDIO_DATA_REG, 0);
    NetRegWrite(GMAC1_MDIO_ADDR_REG, v);

    while ((NetRegRead(GMAC1_MDIO_ADDR_REG) & MDIO_BUSY) != 0) {
        if (NetTimeReached(deadline)) {
            return -1;
        }
    }
    *val = (uint16_t)(NetRegRead(GMAC1_MDIO_DATA_REG) & 0xffffu);
    return 0;
}

/* ------------------------------------------------------------------ */
/* RX ring maintenance                                                */
/* ------------------------------------------------------------------ */

static void NetRxArm(uint32_t slot)
{
    g_rx_desc[slot].d0 = (uint32_t)(uintptr_t)g_rx_pool[slot];
    g_rx_desc[slot].d1 = 0;
    g_rx_desc[slot].d2 = 0;
    NetDsb();
    g_rx_desc[slot].d3 = RDES3_OWN | RDES3_IOC | RDES3_BUF1V;
    g_rx_state[slot] = SLOT_FREE;
}

static void NetRxKick(void)
{
    /* Full-ring tail: the DMA may walk the whole ring and wraps on its
     * own; descriptors still owned by software simply backpressure it. */
    NetDsb();
    NetRegWrite(DMA_CHAN_RX_END_ADDR,
                 (uint32_t)(uintptr_t)&g_rx_desc[N_DESC - 1]);
}

/* Reap completed RX descriptors into the ready queue (bounded by ring
 * size). Called from the ISR and from NetRecv in polling mode. */
static void NetRxReap(void)
{
    uint32_t processed = 0;

    while (processed < N_DESC) {
        uint32_t slot = g_rx_cur;
        uint32_t d3 = g_rx_desc[slot].d3;
        uint32_t queued = 0;

        if ((d3 & RDES3_OWN) != 0) {
            break;                       /* DMA still owns it: nothing new */
        }
        if (g_rx_state[slot] != SLOT_FREE) {
            break;                       /* wrap onto a queued frame */
        }

        if ((d3 & RDES3_CTXT) != 0) {
            /* context (timestamp/PTR) descriptor: no frame */
            g_dev.stats.rx_dropped++;
        } else if (((d3 & (RDES3_FD | RDES3_LD)) != (RDES3_FD | RDES3_LD)) ||
                   ((d3 & RDES3_ERROR_SUMMARY) != 0)) {
            /* split frame (RBSZ > MTU, unexpected) or errored frame */
            g_dev.stats.rx_errors++;
        } else {
            uint32_t len = d3 & RDES3_PACKET_SIZE_MASK;
            if ((len < 14u) || (len > FRAME_SIZE)) {
                g_dev.stats.rx_dropped++;
            } else if (((g_rdy_tail + 1u) % N_DESC) == g_rdy_head) {
                g_dev.stats.rx_overflow++;
                g_dev.stats.rx_dropped++;
            } else {
                /* keep the descriptor software-owned here: the frame data
                 * in g_rx_pool must survive until Recv() copies it out.
                 * Arming now would hand the buffer back to the DMA while
                 * the consumer may still read it (data-tearing window,
                 * fixed 2026-09-27). Recv() arms after the copy. */
                g_rx_len[slot] = (uint16_t)len;
                g_rx_state[slot] = SLOT_READY;
                g_rdy_q[g_rdy_tail] = (uint8_t)slot;
                g_rdy_tail = (g_rdy_tail + 1u) % N_DESC;
                queued = 1;
            }
        }

        if (queued == 0) {
            /* only recycle slots that did NOT enter the queue */
            NetRxArm(slot);
        }
        g_rx_cur = (g_rx_cur + 1u) % N_DESC;
        processed++;
    }

    if (processed != 0) {
        NetRxKick();                    /* release the window */
    }
}

/* ------------------------------------------------------------------ */
/* TX ring maintenance (lazy completion reaping, no TIE)              */
/* ------------------------------------------------------------------ */

/* Reap cursor: index of the oldest SLOT_PENDING descriptor. Kept
 * separate from g_tx_alloc (next free slot) because pending frames are
 * always *behind* the allocation cursor - starting the scan at g_tx_alloc
 * would stop on the first free slot and never reap anything. */
static uint32_t g_tx_reap;

static void NetTxReap(void)
{
    while (g_tx_state[g_tx_reap] == SLOT_PENDING) {
        if ((g_tx_desc[g_tx_reap].d3 & TDES3_OWN) != 0) {
            break;                       /* DMA still owns it */
        }
        if ((g_tx_desc[g_tx_reap].d3 & RDES3_ERROR_SUMMARY) != 0) {
            g_dev.stats.tx_errors++;
        }
        g_dev.stats.tx_completed++;
        g_tx_state[g_tx_reap] = SLOT_FREE;
        g_tx_reap = (g_tx_reap + 1u) % N_DESC;
    }
}

/* One-shot diagnostic snapshot: DMA engine state + first TX/RX descriptors.
 * Auto-fires on the first tx EAGAIN (ring full with zero completions) to
 * distinguish "DMA never fetched" (OWN still set, TS state machine idle)
 * from "fetch broke mid-way" (FBE/CDE latched in channel status). */
static void NetDiagDump(const char *tag)
{
    PRINTK("[net] DIAG %s: MODE=%#x BUS=%#x DBG0=%#x CH_STA=%#x\n", tag,
           NetRegRead(DMA_BUS_MODE), NetRegRead(DMA_SYS_BUS_MODE),
           NetRegRead(DMA_DEBUG_STATUS_0), NetRegRead(DMA_CHAN_STATUS));
    PRINTK("[net] DIAG %s: TXCTL=%#x RXCTL=%#x TXB=%#x RXB=%#x TXE=%#x RXE=%#x\n",
           tag, NetRegRead(DMA_CHAN_TX_CONTROL), NetRegRead(DMA_CHAN_RX_CONTROL),
           NetRegRead(DMA_CHAN_TX_BASE), NetRegRead(DMA_CHAN_RX_BASE),
           NetRegRead(DMA_CHAN_TX_END_ADDR), NetRegRead(DMA_CHAN_RX_END_ADDR));
    PRINTK("[net] DIAG %s: TXLEN=%#x RXLEN=%#x TXd0[0]=%#x TXd3[0]=%#x TXd3[1]=%#x RXd0[0]=%#x RXd3[0]=%#x\n",
           tag, NetRegRead(DMA_CHAN_TX_RING_LEN), NetRegRead(DMA_CHAN_RX_RING_LEN),
           g_tx_desc[0].d0, g_tx_desc[0].d3, g_tx_desc[1].d3,
           g_rx_desc[0].d0, g_rx_desc[0].d3);
    PRINTK("[net] DIAG %s: alloc=%u reap=%u rx_cur=%u accepted=%u completed=%u busy=%u\n",
           tag, g_tx_alloc, g_tx_reap, g_rx_cur,
           g_dev.stats.tx_accepted, g_dev.stats.tx_completed, g_dev.stats.tx_busy);
}

/* ------------------------------------------------------------------ */
/* Interrupt service (interrupt mode)                                 */
/* ------------------------------------------------------------------ */

static VOID NetIsr(VOID)
{
    uint32_t status = NetRegRead(DMA_CHAN_STATUS);
    uint32_t clr = status & (DMA_CHAN_STATUS_RI | DMA_CHAN_STATUS_RBU |
                             DMA_CHAN_STATUS_FBE | DMA_CHAN_STATUS_CDE |
                             DMA_CHAN_STATUS_AIS | DMA_CHAN_STATUS_NIS);

    /* W1C on the bits we handle */
    NetRegWrite(DMA_CHAN_STATUS, clr);

    g_dev.stats.irq_count++;

    if ((status & (DMA_CHAN_STATUS_RI | DMA_CHAN_STATUS_RBU)) != 0) {
        NetRxReap();
    }
    if ((status & DMA_CHAN_STATUS_AIS) != 0) {
        if ((status & (DMA_CHAN_STATUS_FBE | DMA_CHAN_STATUS_CDE)) != 0) {
            g_dev.stats.dma_errors++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Contract implementation                                            */
/* ------------------------------------------------------------------ */

int NetInit(const NetConfig *cfg, NetDevice **dev)
{
    uint32_t val;
    static const uint8_t mac[6] = GMAC1_DEFAULT_MAC;
    uint64_t deadline;

    if ((cfg == NULL) || (dev == NULL)) {
        return NET_EINVAL;
    }
    if (cfg->port_id != NET_PORT_GMAC1_RK3588) {
        return NET_ENODEV;           /* unknown/not handed-over port */
    }
    if (cfg->mode > NET_MODE_HYBRID) {
        return NET_EINVAL;
    }
    if (cfg->autoneg == 0) {
        /* no forced-link policy on this port (net.h: YT9214 fixed-1000M
         * RGMII, no forceable PHY behind the MAC) */
        return NET_ENOTSUP;
    }
    if (g_dev.state == NET_STATE_RUNNING) {
        return NET_EBUSY;           /* running: Stop first */
    }
    /* OFFLINE or READY both reach here. READY = reconfigure with a new
     * NetConfig (e.g. switch mode): Init is a full reprogram anyway -
     * DMA soft reset + every register rewritten - so re-entry from
     * READY is side-effect free. */

    /* --- instance selection: read the ctrl v2 block BEFORE touching
     *     MDIO so the probe lands on the port the master actually
     *     handed over (gmac0 fe1b0000 / rtl8211f @0x0 via module
     *     param gmac=0). A v1 or absent ctrl keeps the gmac1 defaults. */
    {
        volatile struct net_ctrl *c =
            (volatile struct net_ctrl *)NET_CTRL_PHYS;
        int have_v2 = ((c->magic == NET_CTRL_MAGIC) &&
                       (c->version == NET_CTRL_VERSION));

        if (have_v2) {
            if (c->mac_base != 0u) {
                g_mac_base = c->mac_base;
            }
            if (c->irq_intid != 0u) {
                g_irq_intid = c->irq_intid;
            }
            if (c->phy_addr != 0xffffffffu) {
                g_phy_addr = c->phy_addr;
            }
        }
        PRINTK("[net] instance base=%#x intid=%u phy_addr=0x%x (%s)\n",
               (uint32_t)g_mac_base, g_irq_intid, g_phy_addr,
               have_v2 ? "ctrl v2" : "ctrl v1/absent: gmac1 defaults");
    }

    /* --- informational PHY probe: PHYSID1 at the instance address. An
     *     unmanaged switch stays silent (0xffff) - tolerated, the
     *     master publishes the fixed 1000M link. No writes ever. --- */
    {
        uint16_t id1 = 0;

        PRINTK("[net] mdio probe enter\n");
        if ((NetMdioRead(g_phy_addr, 2 /* PHYSID1 */, &id1) != 0) ||
            (id1 == 0xffffu) || (id1 == 0u)) {
            PRINT_ERR("[net] no PHY at MDIO 0x%x - fixed 1000M RGMII "
                      "assumed (YT9214 unmanaged strapping)\n",
                      g_phy_addr);
        }
        PRINTK("[net] mdio probe done id1=0x%x\n", id1);
    }

    /* --- DMA soft reset: DO NOT write SWR=1. Measured on bq3588hm
     * (2026-10-16): writing SWR=1 puts the engine into a reset it never
     * leaves (reads 1 forever, channel regs partially drop writes - e.g.
     * SYS_BUS_MODE 0x11 written reads back 0x1 - both classic
     * reset-in-progress behaviour), and the follow-up tx path then never
     * completes a single descriptor (a16/c0, tx EAGAIN cascade). The dwmac
     * 4.20a IP here (VERSION=0x3051) runs without the global SWR.
     * Power-on default observed in earlier runs was never
     * read BEFORE writing, so it is now: print it, force-clear it if
     * already stuck at 1, never request the reset ourselves. --- */
    {
        uint32_t mode0 = NetRegRead(DMA_BUS_MODE);
        PRINTK("[net] DMA_MODE power-on=%#x SWR=%u\n",
               mode0, (mode0 & DMA_BUS_MODE_SFT_RESET) ? 1u : 0u);
        if ((mode0 & DMA_BUS_MODE_SFT_RESET) != 0) {
            /* try to knock it out of a stuck reset; harmless if clean */
            NetRegWrite(DMA_BUS_MODE, mode0 & ~(uint32_t)DMA_BUS_MODE_SFT_RESET);
            deadline = NetCntpct() + NetUsToTicks(1000u);
            while ((NetRegRead(DMA_BUS_MODE) & DMA_BUS_MODE_SFT_RESET) != 0) {
                if (NetTimeReached(deadline)) {
                    PRINTK("[net] SWR stuck-at-1, force-clear failed - continue\n");
                    break;
                }
            }
        }
    }
    PRINTK("[net] dma reset phase done (no SWR write)\n");

    /* --- AXI bus mode: 32-bit addressing, fixed burst, short bursts.
     * Read-modify-write: the reset default 0x1010000
     * carries integration bits that a blind overwrite would drop, and a
     * dropped default was measured to stick (write 0x11 -> read 0x1 while
     * SWR displayed stuck). --- */
    NetRegWrite(DMA_SYS_BUS_MODE,
                 NetRegRead(DMA_SYS_BUS_MODE) |
                 DMA_SYS_BUS_MODE_FB | DMA_SYS_BUS_BLEN4 | DMA_SYS_BUS_BLEN8 |
                 DMA_SYS_BUS_BLEN16 | DMA_SYS_BUS_BLEN32);

    /* --- MAC static config: this port ALWAYS runs GMII/1000M full -
     *     the YT9214 keeps the RGMII side at 1000M regardless of the
     *     copper-side speed (switch buffers between ports), so there
     *     are no speed-class bits to track (PS/FES stay clear). Rx
     *     filter per config: promiscuous (sniffer/ECAT) or
     *     DA0+multicast. --- */
    val = NetRegRead(GMAC_CONFIG);
    val &= ~(GMAC_CONFIG_PS | GMAC_CONFIG_FES | GMAC_CONFIG_TE | GMAC_CONFIG_RE);
    val |= GMAC_CONFIG_DM | GMAC_CONFIG_JD | GMAC_CONFIG_BE |
           GMAC_CONFIG_DCRS;
    NetRegWrite(GMAC_CONFIG, val);

    if (cfg->promisc != 0) {
        NetRegWrite(GMAC_PACKET_FILTER,
                     GMAC_PACKET_FILTER_PR | GMAC_PACKET_FILTER_PM |
                     GMAC_PACKET_FILTER_HMC | GMAC_PACKET_FILTER_PCF);
    } else {
        /* unicast -> GMAC_ADDR0 filter, multicast pass-all, control in */
        NetRegWrite(GMAC_PACKET_FILTER,
                     GMAC_PACKET_FILTER_PM | GMAC_PACKET_FILTER_HMC |
                     GMAC_PACKET_FILTER_PCF);
    }

    NetRegWrite(GMAC_ADDR0_HIGH, ((uint32_t)mac[5] << 8) | mac[4] |
                  (1u << 31));  /* always valid */
    NetRegWrite(GMAC_ADDR0_LOW, ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
                  ((uint32_t)mac[1] << 8) | mac[0]);

    /* --- MTL: single queue, store-and-forward both ways --- */
    NetRegWrite(MTL_OPERATION_MODE, MTL_OP_SCHALG_SP | MTL_OP_RAA_WSP);
    NetRegWrite(MTL_RXQ_DMA_MAP0, 0);   /* RX queue 0 -> DMA channel 0 */

    val = NetRegRead(MTL_TXQ0_OP_MODE);
    val |= MTL_OP_MODE_TXQEN | MTL_OP_MODE_TSF | MTL_OP_MODE_TTC_32;
    NetRegWrite(MTL_TXQ0_OP_MODE, val);

    val = NetRegRead(MTL_RXQ0_OP_MODE);
    val |= MTL_OP_MODE_RSF | MTL_OP_MODE_RTC_64;
    NetRegWrite(MTL_RXQ0_OP_MODE, val);

    /* --- DMA channel 0: rings, buffer size, engines still stopped --- */
    NetRegWrite(DMA_CHAN_TX_BASE, (uint32_t)(uintptr_t)g_tx_desc);
    NetRegWrite(DMA_CHAN_RX_BASE, (uint32_t)(uintptr_t)g_rx_desc);
    NetRegWrite(DMA_CHAN_TX_RING_LEN, N_DESC - 1);
    NetRegWrite(DMA_CHAN_RX_RING_LEN, N_DESC - 1);

    NetRegWrite(DMA_CHAN_TX_CONTROL,
                 (GMAC1_DMA_PBL << DMA_TXPBL_SHIFT) | DMA_CONTROL_OSP);
    NetRegWrite(DMA_CHAN_RX_CONTROL,
                 ((FRAME_SIZE << DMA_RBSZ_SHIFT) & DMA_RBSZ_MASK));

    /* --- build the RX ring, wipe the TX ring --- */
    for (uint32_t i = 0; i < N_DESC; i++) {
        NetRxArm(i);
        g_tx_desc[i].d0 = 0;
        g_tx_desc[i].d1 = 0;
        g_tx_desc[i].d2 = 0;
        g_tx_desc[i].d3 = 0;
        g_tx_state[i] = SLOT_FREE;
    }
    g_rx_cur = 0;
    g_tx_alloc = 0;
    g_tx_reap = 0;
    g_rdy_head = 0;
    g_rdy_tail = 0;

    g_dev.port_id = cfg->port_id;
    g_dev.mode = cfg->mode;
    g_dev.promisc = (cfg->promisc != 0) ? 1u : 0u;
    g_dev.ctrl = (volatile struct net_ctrl *)NET_CTRL_PHYS;
    g_dev.state = NET_STATE_READY;

    if ((g_dev.ctrl->magic == NET_CTRL_MAGIC) &&
        (g_dev.ctrl->version == NET_CTRL_VERSION)) {
        g_dev.ctrl->slave_state = NET_STATE_READY;
    }

    *dev = &g_dev;
    return 0;
}

int NetStart(NetDevice *dev)
{
    if ((dev == NULL) || (dev != &g_dev)) {
        return NET_EINVAL;
    }
    if (dev->state == NET_STATE_RUNNING) {
        return 0;                        /* idempotent */
    }
    if (dev->state != NET_STATE_READY) {
        return NET_EIO;               /* FAULT: needs Stop first */
    }

    if (dev->mode != NET_MODE_POLL) {
        if (ArchHwiCreate((HWI_HANDLE_T)g_irq_intid, GMAC1_IRQ_LOS_PRIO,
                          HWI_MODE_TRIGGER_LEVEL,
                          (HWI_PROC_FUNC)NetIsr, (HwiIrqParam *)0) != LOS_OK) {
            PRINT_ERR("[net] ArchHwiCreate(%u) failed\n", g_irq_intid);
            return NET_EIO;
        }
    }

    /* clear any latched status, arm interrupts (masked out in poll mode:
     * the MAC stays silent on the wire and Recv() drives everything) */
    NetRegWrite(DMA_CHAN_STATUS, 0xffffffffu);
    if (dev->mode != NET_MODE_POLL) {
        NetRegWrite(DMA_CHAN_INTR_ENA,
                     DMA_CHAN_INTR_ENA_NIE | DMA_CHAN_INTR_ENA_AIE |
                     DMA_CHAN_INTR_ENA_RIE | DMA_CHAN_INTR_ENA_RBUE |
                     DMA_CHAN_INTR_ENA_FBE | DMA_CHAN_INTR_ENA_CDE);
    } else {
        NetRegWrite(DMA_CHAN_INTR_ENA, 0);
    }

    /* engine bring-up order: tail ptr, then DMA SR/ST,
     * then MAC RE/TE */
    NetRxKick();
    NetRegWrite(DMA_CHAN_RX_CONTROL,
                 NetRegRead(DMA_CHAN_RX_CONTROL) | DMA_CONTROL_SR);
    NetRegWrite(GMAC_CONFIG, NetRegRead(GMAC_CONFIG) | GMAC_CONFIG_RE);

    /* TX tail at base before starting the engine: with the open-interval
     * [cursor, tail) fetch
     * semantics this arms an empty ring; every NetSend doorbell then
     * points at the next free slot and advances the window. */
    NetRegWrite(DMA_CHAN_TX_END_ADDR, (uint32_t)(uintptr_t)g_tx_desc);
    NetRegWrite(DMA_CHAN_TX_CONTROL,
                 NetRegRead(DMA_CHAN_TX_CONTROL) | DMA_CONTROL_ST);
    NetRegWrite(GMAC_CONFIG, NetRegRead(GMAC_CONFIG) | GMAC_CONFIG_TE);

    dev->state = NET_STATE_RUNNING;
    if ((dev->ctrl->magic == NET_CTRL_MAGIC) &&
        (dev->ctrl->version == NET_CTRL_VERSION)) {
        dev->ctrl->slave_state = NET_STATE_RUNNING;
    }
    NetDiagDump("started");
    return 0;
}

int NetStop(NetDevice *dev, uint32_t timeout_us)
{
    uint64_t deadline;
    uint32_t quiet = 0;

    if ((dev == NULL) || (dev != &g_dev)) {
        return NET_EINVAL;
    }
    if (dev->state != NET_STATE_RUNNING) {
        return 0;
    }

    /* mask everything first so the ISR cannot restart engines */
    NetRegWrite(DMA_CHAN_INTR_ENA, 0);
    NetRegWrite(DMA_CHAN_STATUS, 0xffffffffu);

    NetRegWrite(DMA_CHAN_TX_CONTROL,
                 NetRegRead(DMA_CHAN_TX_CONTROL) & ~DMA_CONTROL_ST);
    NetRegWrite(DMA_CHAN_RX_CONTROL,
                 NetRegRead(DMA_CHAN_RX_CONTROL) & ~DMA_CONTROL_SR);
    NetRegWrite(GMAC_CONFIG,
                 NetRegRead(GMAC_CONFIG) & ~(GMAC_CONFIG_TE | GMAC_CONFIG_RE));

    /* bounded quiesce: DMA_DEBUG_STATUS_0 TS[3:0]/RS[7:4] both 0.
     * timeout_us == 0 polls once and fails fast (generic-timer based:
     * core frequency independent). */
    deadline = NetCntpct() + NetUsToTicks(timeout_us);
    for (;;) {
        uint32_t dbg = NetRegRead(DMA_DEBUG_STATUS_0);
        if (((dbg & 0xfu) == 0) && (((dbg >> 4) & 0xfu) == 0)) {
            quiet = 1;
            break;
        }
        if (NetTimeReached(deadline)) {
            break;
        }
    }
    if (quiet == 0) {
        dev->state = NET_STATE_FAULT;
        if ((dev->ctrl->magic == NET_CTRL_MAGIC) &&
            (dev->ctrl->version == NET_CTRL_VERSION)) {
            dev->ctrl->slave_state = NET_STATE_FAULT;
        }
        PRINT_ERR("[net] Stop: DMA quiesce timeout\n");
        return NET_ETIMEDOUT;
    }

    if (dev->mode != NET_MODE_POLL) {
        /* ArchHwiDelete masks the SPI at the GICD (per-core ownership) */
        (VOID)ArchHwiDelete((HWI_HANDLE_T)g_irq_intid, (HwiIrqParam *)0);
    }

    /* reset the rings for the next Start */
    for (uint32_t i = 0; i < N_DESC; i++) {
        NetRxArm(i);
        g_tx_desc[i].d3 = 0;
        g_tx_state[i] = SLOT_FREE;
    }
    g_rx_cur = 0;
    g_tx_alloc = 0;
    g_tx_reap = 0;
    g_rdy_head = 0;
    g_rdy_tail = 0;

    dev->state = NET_STATE_READY;
    if ((dev->ctrl->magic == NET_CTRL_MAGIC) &&
        (dev->ctrl->version == NET_CTRL_VERSION)) {
        dev->ctrl->slave_state = NET_STATE_READY;
    }
    return 0;
}

int NetSend(NetDevice *dev, const uint8_t *frame, uint32_t len)
{
    uint32_t slot;

    if ((dev == NULL) || (dev != &g_dev) || (frame == NULL)) {
        return NET_EINVAL;
    }
    if (dev->state != NET_STATE_RUNNING) {
        return NET_EIO;
    }
    if ((len < 14u) || (len > 1514u)) {
        return NET_EMSGSIZE;
    }

    NetTxReap();

    /* find a free slot: with single-producer allocation the free slots
     * form a contiguous run starting at g_tx_alloc */
    for (slot = 0; slot < N_DESC; slot++) {
        uint32_t idx = (g_tx_alloc + slot) % N_DESC;
        if (g_tx_state[idx] == SLOT_FREE) {
            g_tx_alloc = idx;
            break;
        }
    }
    if (slot == N_DESC) {
        dev->stats.tx_busy++;
        static uint8_t diag_fired = 0;
        if (diag_fired == 0) {
            diag_fired = 1;
            NetDiagDump("tx-eagain");
        }
        return NET_EAGAIN;
    }

    memcpy(g_tx_pool[g_tx_alloc], frame, len);

    g_tx_desc[g_tx_alloc].d0 = (uint32_t)(uintptr_t)g_tx_pool[g_tx_alloc];
    g_tx_desc[g_tx_alloc].d1 = 0;
    g_tx_desc[g_tx_alloc].d2 = len & TDES2_B1SIZE_MASK;
    NetDsb();
    g_tx_desc[g_tx_alloc].d3 = TDES3_OWN | TDES3_FD | TDES3_LD |
                               (len & TDES3_PACKET_SIZE_MASK);
    g_tx_state[g_tx_alloc] = SLOT_PENDING;

    /* Doorbell semantics: the DMA fetches descriptors in [cursor,
     * tail) - the descriptor the tail points at is NOT fetched. The
     * tail must therefore be written at the NEXT free slot, after the
     * allocation cursor advanced. Pointing the tail at the descriptor
     * we just armed makes the DMA
     * skip it entirely (OWN never cleared, TBU latched; observed
     * 2026-09-23). */
    g_tx_alloc = (g_tx_alloc + 1u) % N_DESC;
    NetRegWrite(DMA_CHAN_TX_END_ADDR,
                 (uint32_t)(uintptr_t)&g_tx_desc[g_tx_alloc]);

    dev->stats.tx_accepted++;
    return 0;
}

int NetRecv(NetDevice *dev, uint8_t *frame, uint32_t capacity, uint32_t *len)
{
    uint32_t slot, flen;

    if ((dev == NULL) || (dev != &g_dev) || (frame == NULL) || (len == NULL)) {
        return NET_EINVAL;
    }
    if (dev->state != NET_STATE_RUNNING) {
        return NET_EIO;
    }

    /* POLL: Recv() is the only reaper. HYBRID: the ISR reaps ahead of us;
     * reaping here too is idempotent and backstops a dropped IRQ. */
    if (dev->mode != NET_MODE_IRQ) {
        NetRxReap();
    }

    while (g_rdy_head != g_rdy_tail) {
        slot = g_rdy_q[g_rdy_head];
        /* NOTE: g_rx_state[slot] is NOT checked here - the slot was armed
         * back to SLOT_FREE right after being queued (the frame data lives
         * in g_rx_pool until Recv copies it out). The SPSC ring itself
         * guarantees the index/len pair is consistent. */
        flen = g_rx_len[slot];
        if (flen > capacity) {
            /* contract: short buffer -> drop the frame and count it */
            g_rdy_head = (g_rdy_head + 1u) % N_DESC;
            NetRxArm(slot);
            NetRxKick();
            dev->stats.rx_dropped++;
            return NET_EMSGSIZE;
        }

        memcpy(frame, g_rx_pool[slot], flen);

        g_rdy_head = (g_rdy_head + 1u) % N_DESC;
        NetRxArm(slot);
        NetRxKick();

        dev->stats.rx_delivered++;
        *len = flen;
        return 0;
    }
    return NET_EAGAIN;
}

int NetGetLinkInfo(NetDevice *dev, NetLinkInfo *info)
{
    if ((dev == NULL) || (dev != &g_dev) || (info == NULL)) {
        return NET_EINVAL;
    }

    if ((dev->ctrl->magic != NET_CTRL_MAGIC) ||
        (dev->ctrl->version != NET_CTRL_VERSION)) {
        memset(info, 0, sizeof(*info));
        return 0;                        /* master not up: invalid snapshot */
    }

    /* the 64-bit timestamp is written by another core: double-read valid.
     * Bounded retries: a master write tearing the snapshot mid-read must
     * not spin forever (observed as a hung net_demo with zero stats
     * output, 2026-10-18). */
    {
        uint32_t retries = 0;

        do {
            info->valid = dev->ctrl->valid;
            info->link_up = dev->ctrl->link_up;
            info->speed_mbps = dev->ctrl->speed_mbps;
            info->full_duplex = dev->ctrl->full_duplex;
            info->sample_time_ns = dev->ctrl->sample_time_ns;
        } while ((info->valid != dev->ctrl->valid) &&
                 (++retries < 100u));
        if (retries >= 100u) {
            dev->stats.rx_errors++;   /* counter abuse: ctrl tearing seen */
        }
    }

    /* no forced-link policy on this port: the master-published snapshot
     * (YT9214 copper state, or the fixed 1000M when unmanaged) IS the
     * truth. The MAC side always runs 1000M (net.h). */
    return 0;
}

int NetGetStats(NetDevice *dev, NetStats *stats)
{
    if ((dev == NULL) || (dev != &g_dev) || (stats == NULL)) {
        return NET_EINVAL;
    }

    NetTxReap();                        /* fold in lazy TX completions */
    *stats = dev->stats;
    if ((dev->ctrl->magic == NET_CTRL_MAGIC) &&
        (dev->ctrl->version == NET_CTRL_VERSION)) {
        stats->link_changes = dev->ctrl->link_changes;
    }
    return 0;
}

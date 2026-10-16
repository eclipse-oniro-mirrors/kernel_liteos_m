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
 * net_dwmac.c - real-time NIC data plane for the AMP slave core on the
 * RK3568 dwmac-4.20a GMAC1 (fe010000, RGMII, YT8531S PHY).
 * protocol-agnostic: any higher-layer protocol rides the same frame-level
 * contract (net.h, net* surface).
 * the linux master (mcs_net.ko) owns clocks/reset/pinmux/GRF, PHY bring-up
 * and mac_speed tracking, and must be loaded BEFORE the slave touches
 * GMAC1 (board/DRIVER_GUIDE.md); this file owns the RX/TX data path, its
 * interrupt (ArchHwiCreate -> per-core GICD ownership) and the on-demand
 * MDIO link query (NetGetLinkInfo).
 * lifecycle: NetInit validates config, optionally forces the PHY via MDIO,
 * resets the DMA, programs MTL/DMA chan 0 and builds the static descriptor
 * rings (no dynamic allocation); NetStart arms DMA irqs in IRQ/HYBRID
 * (POLL keeps the MAC silent, recv() drives); NetSend rings the doorbell,
 * TX completion reaped lazily; NetRecv dequeues from the ready queue and
 * re-arms; NetStop quiesces within a bounded timeout and deregisters IRQ.
 * memory model: the DMA pools live in the dedicated normal non-cacheable
 * window at NET_DMA_BASE (los_mmu.c), so PA==VA holds and descriptors/
 * buffers are DMA-coherent with the d-cache on. NetInit rewrites every
 * descriptor, so the pools do not rely on .bss zeroing.
 */

#include <stdint.h>
#include <string.h>
#include "los_compiler.h"
#include "los_debug.h"
#include "los_interrupt.h"
#include "los_tick.h"
#include "los_arch_interrupt.h"
#include "net.h"
#include "gmac1_profile.h"

/* ------------------------------------------------------------------ */
/* register map (dwmac-4.20a, all offsets relative to GMAC1_BASE)      */
/* ------------------------------------------------------------------ */

/* MAC */
#define MAC_CONFIG             0x0000u
#define MAC_PKT_FILTER         0x0008u
#define MAC_ADDR0_HIGH         0x0300u
#define MAC_ADDR0_LOW          0x0304u

#define MAC_CFG_BE             (1u << 18)
#define MAC_CFG_JD             (1u << 17)
#define MAC_CFG_PS             (1u << 15)
#define MAC_CFG_FES            (1u << 14)
#define MAC_CFG_DM             (1u << 13)
#define MAC_CFG_DCRS           (1u << 9)
#define MAC_CFG_TE             (1u << 1)
#define MAC_CFG_RE             (1u << 0)

#define MAC_FLT_PR             (1u << 0)
#define MAC_FLT_HMC            (1u << 2)
#define MAC_FLT_PM             (1u << 4)
#define MAC_FLT_PCF            (1u << 7)

/* MTL */
#define MTL_OP_MODE            0x0c00u
#define MTL_RXQ_MAP0           0x0c30u
#define MTL_TXQ0_MODE          0x0d00u
#define MTL_RXQ0_MODE          0x0d30u

#define MTL_SCHALG_SP          (0x3u << 5)
#define MTL_RAA_WSP            (1u << 2)
#define MTL_QMODE_RSF          (1u << 5)
#define MTL_QMODE_TXQEN        (1u << 3)
#define MTL_QMODE_TSF          (1u << 1)
#define MTL_QMODE_TTC_32       0u
#define MTL_QMODE_RTC_64       0u

/* DMA */
#define DMA_BUS_CFG            0x1000u
#define DMA_SYS_BUS_CFG        0x1004u
#define DMA_DBG_STATUS         0x100cu

#define DMA_BUS_CFG_SW_RESET   (1u << 0)

#define DMA_SYS_BUS_CFG_FB     (1u << 0)
#define DMA_SYS_BUS_BL4        (1u << 1)
#define DMA_SYS_BUS_BL8        (1u << 2)
#define DMA_SYS_BUS_BL16       (1u << 3)
#define DMA_SYS_BUS_BL32       (1u << 4)

/* DMA channel 0 register block (0x1100 + ch * 0x80, ch = 0 here) */
#define DMA_CH_TX_CTRL         0x1104u
#define DMA_CH_RX_CTRL         0x1108u
#define DMA_CH_TX_LIST         0x1114u
#define DMA_CH_RX_LIST         0x111cu
#define DMA_CH_TX_TAIL         0x1120u   /* TX tail pointer / doorbell */
#define DMA_CH_RX_TAIL         0x1128u   /* RX tail pointer */
#define DMA_CH_TX_LEN          0x112cu
#define DMA_CH_RX_LEN          0x1130u
#define DMA_CH_INT_EN          0x1134u
#define DMA_CH_STATUS          0x1160u

#define DMA_CH_ST              (1u << 0)
#define DMA_CH_SR              (1u << 0)
#define DMA_CH_OSP             (1u << 4)
#define DMA_CH_PBL_SHIFT       16
#define DMA_CH_RBSZ_SHIFT      1
#define DMA_CH_RBSZ_MASK       0x3fffu   /* 14-bit field at bit 1 */

#define DMA_CH_RI              (1u << 6)
#define DMA_CH_RBU             (1u << 7)
#define DMA_CH_FBE             (1u << 12)
#define DMA_CH_CDE             (1u << 13)
#define DMA_CH_AIS             (1u << 14)
#define DMA_CH_NIS             (1u << 15)

#define DMA_CH_INT_NIE         (1u << 16)
#define DMA_CH_INT_AIE         (1u << 15)
#define DMA_CH_INT_CDE         (1u << 13)
#define DMA_CH_INT_FBE         (1u << 12)
#define DMA_CH_INT_RBUE        (1u << 7)
#define DMA_CH_INT_RIE         (1u << 6)

/* DMA_DBG_STATUS: TX state in [3:0], RX state in [7:4]; 0 = stopped */

/* enhanced (16-byte) descriptor words */
struct netDmaDesc {
    volatile uint32_t d0;   /* read: buffer 1 address */
    volatile uint32_t d1;   /* read: buffer 2 address (unused) */
    volatile uint32_t d2;   /* TX read: buffer 1 size; RX write-back: extra status */
    volatile uint32_t d3;   /* read: OWN/FD/LD/...; write-back status */
};

#define TXD2_BUF1_LEN_MASK     0x3fffu
#define TXD3_LEN_MASK          0x7fffu
#define TXD3_LD                (1u << 28)
#define TXD3_FD                (1u << 29)
#define TXD3_OWN               (1u << 31)

#define RXD3_LEN_MASK          0x7fffu
#define RXD3_ERR_SUM           (1u << 15)
#define RXD3_BUF1V             (1u << 24)
#define RXD3_IOC               (1u << 30)
#define RXD3_LD                (1u << 28)
#define RXD3_FD                (1u << 29)
#define RXD3_CTXT              (1u << 30)
#define RXD3_OWN               (1u << 31)

/* ------------------------------------------------------------------ */
/* static pools (contract: no dynamic allocation)                     */
/* ------------------------------------------------------------------ */

#define N_DESC      NET_DESC_NUM
#define FRAME_SIZE  NET_FRAME_SIZE

/* DMA pools inside the NC window (see gmac1_profile.h for the layout
 * contract). const pointers keep every existing g_rx_desc[i] /
 * g_tx_pool[i][j] / &g_rx_desc[N_DESC-1] expression source-compatible
 * with the old static-array form. */
static struct netDmaDesc *const g_rxDesc =
    (struct netDmaDesc *)(NET_DMA_BASE + NET_DMA_RX_DESC_OFF);
static struct netDmaDesc *const g_txDesc =
    (struct netDmaDesc *)(NET_DMA_BASE + NET_DMA_TX_DESC_OFF);
static uint8_t (*const g_rxPool)[FRAME_SIZE] =
    (uint8_t (*)[FRAME_SIZE])(NET_DMA_BASE + NET_DMA_RX_POOL_OFF);
static uint8_t (*const g_txPool)[FRAME_SIZE] =
    (uint8_t (*)[FRAME_SIZE])(NET_DMA_BASE + NET_DMA_TX_POOL_OFF);

/* per-slot software state, indexed the same as the descriptor rings */
#define SLOT_FREE     0u   /* desc armed with OWN, empty (RX) / reusable (TX) */
#define SLOT_READY    1u   /* RX: frame waiting in the ready queue */
#define SLOT_PENDING  2u   /* TX: owned by DMA (or completed, not yet reaped) */

static uint8_t g_rxState[N_DESC];
static uint8_t g_txState[N_DESC];
static uint16_t g_rxLen[N_DESC];

/* RX ready queue: lock-free SPSC ring of slot indices. producer = the ISR
 * (NetRxReap), consumer = the task (NetRecv), both on CPU3; head is written
 * only by the consumer, tail only by the producer, and no shared RMW
 * counter exists (a shared g_rdy_cnt race corrupted the queue once,
 * observed as a data-abort hang 2026-09-27). capacity N_DESC-1. */
static uint8_t g_rdyQ[N_DESC];
static volatile uint32_t g_rdyHead, g_rdyTail;

/* cursor of the RX reap scan (next desc the ISR expects to be OWN) */
static uint32_t g_rxCur;
/* TX allocation cursor: free slots are always a contiguous tail here */
static uint32_t g_txAlloc;

struct netDevice {
    uint32_t state;                     /* NET_STATE_* (local view) */
    uint32_t portId;                   /* NET_PORT_* this instance serves */
    uint32_t mode;                      /* NET_MODE_* (runtime, from config) */
    uint32_t autoneg;                   /* 1=autoneg, 0=forced link */
    uint32_t forceSpeedMbps;          /* valid when autoneg==0 */
    uint32_t forceFullDuplex;         /* valid when autoneg==0 */
    uint32_t promisc;                   /* 1=promiscuous rx filter */
    uint32_t lastLinkUp;              /* previous NetGetLinkInfo sample */
    uint32_t linkChanges;              /* link transitions counted locally */
    netStats stats;
};

/* local device lifecycle (no shared state with the master) */
#define NET_STATE_OFFLINE      0u      /* BSS zero: never init'ed */
#define NET_STATE_READY        1u      /* init'ed, engines stopped */
#define NET_STATE_RUNNING      2u      /* engines started */
#define NET_STATE_FAULT        3u      /* stop quiesce timeout */

static struct netDevice g_dev = {
    .state = NET_STATE_OFFLINE,
};

/* ------------------------------------------------------------------ */
/* register helpers                                                   */
/* ------------------------------------------------------------------ */

static inline uint32_t NetRegRead(uint32_t off)
{
    return *(volatile uint32_t *)(GMAC1_BASE + off);
}

static inline void NetRegWrite(uint32_t off, uint32_t val)
{
    *(volatile uint32_t *)(GMAC1_BASE + off) = val;
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
 * backend calls them the same way). the gic-v3 port's createIrq applies
 * the per-core ownership contract for sPIs (group1-NS, IROUTER to this
 * core, ICFGR from hwiMode, priority, enable) - no GICD work is needed
 * here, and the master never configures the slave's interrupts. */
/* ------------------------------------------------------------------ */

extern UINT32 ArchHwiCreate(HWI_HANDLE_T hwiNum, HWI_PRIOR_T hwiPrio,
                            HWI_MODE_T hwiMode, HWI_PROC_FUNC hwiHandler,
                            HwiIrqParam *irqParam);
extern UINT32 ArchHwiDelete(HWI_HANDLE_T hwiNum, HwiIrqParam *irqParam);

/* ------------------------------------------------------------------ */
/* MDIO (clause 22) access - link forcing + link status query           */
/*                                                                     */
/* the master mcs_net.ko probe configures the PHY (RGMII delays, aneg) */
/* and stops writing afterwards; its 1s link work only READS the PHY   */
/* (and keeps the RGMII mac_speed clock tracking the resolved rate).   */
/* the slave writes only during NetInit (force policy) and reads on    */
/* each NetGetLinkInfo call. both sides drive the same GO/BUSY         */
/* handshake, so concurrent accesses serialize at the MDIO controller; */
/* a torn transaction is bounded by the poll timeout below and a bad   */
/* sample self-heals on the next call (the query API is idempotent).   */
/* ------------------------------------------------------------------ */

#define GMAC1_MDIO_ADDR_REG     0x0200u
#define GMAC1_MDIO_DATA_REG     0x0204u
#define MDIO_BUSY               (1u << 0)
#define MDIO_GOC_WRITE          (1u << 2)
#define MDIO_GOC_READ           (3u << 2)
#define MDIO_CR_VAL             5u      /* CSR divider for the 125MHz clock */
#define MDIO_PHY_SHIFT          21
#define MDIO_REG_SHIFT          16
#define MDIO_CR_SHIFT           8

/* PHY registers (clause 22) for the link query path */
#define PHY_REG_BMCR            0u
#define PHY_REG_BMSR            1u
#define PHY_REG_LPA             5u
#define PHY_REG_STAT1000        10u
#define BMSR_LSTATUS            (1u << 2)
#define LPA_1000FULL            (1u << 11)  /* MII_STAT1000 partner 1000FD */
#define LPA_100FULL             (1u << 8)
#define LPA_100HALF             (1u << 7)
#define LPA_10FULL              (1u << 6)
#define LPA_10HALF              (1u << 5)

/* PHY BMCR (reg 0) force encodings; autoneg bit (12) left cleared. */
#define PHY_BMCR_SPD100         (1u << 13)
#define PHY_BMCR_FDUP           (1u << 8)
/* PHY BMCR (reg 0) autoneg encodings (restore aneg after a forced config) */
#define PHY_BMCR_ANEG           (1u << 12)
#define PHY_BMCR_ANEG_RESTART   (1u << 9)

static int NetMdioWrite(uint32_t phy, uint32_t reg, uint16_t val)
{
    uint32_t v;
    uint32_t poll;

    NetRegWrite(GMAC1_MDIO_DATA_REG, val);
    v = MDIO_BUSY | MDIO_GOC_WRITE |
        (phy << MDIO_PHY_SHIFT) | (reg << MDIO_REG_SHIFT) |
        (MDIO_CR_VAL << MDIO_CR_SHIFT);
    NetRegWrite(GMAC1_MDIO_ADDR_REG, v);

    for (poll = 0; poll < 100000u; poll++) {
        if ((NetRegRead(GMAC1_MDIO_ADDR_REG) & MDIO_BUSY) == 0) {
            return 0;
        }
    }
    return -1;
}

static int NetMdioRead(uint32_t phy, uint32_t reg, uint16_t *val)
{
    uint32_t v;
    uint32_t poll;

    v = MDIO_BUSY | MDIO_GOC_READ |
        (phy << MDIO_PHY_SHIFT) | (reg << MDIO_REG_SHIFT) |
        (MDIO_CR_VAL << MDIO_CR_SHIFT);
    NetRegWrite(GMAC1_MDIO_DATA_REG, 0);
    NetRegWrite(GMAC1_MDIO_ADDR_REG, v);

    for (poll = 0; poll < 100000u; poll++) {
        if ((NetRegRead(GMAC1_MDIO_ADDR_REG) & MDIO_BUSY) == 0) {
            *val = (uint16_t)(NetRegRead(GMAC1_MDIO_DATA_REG) & 0xffffu);
            return 0;
        }
    }
    return -1;
}

/* force the PHY to a fixed speed/duplex (no autoneg). 1000BASE-t is
 * rejected: IEEE 802.3 requires autonegotiation for 1000M links.
 * returns 0 on success. */
static int NetPhyForce(uint32_t speedMbps, uint32_t fullDuplex)
{
    uint16_t bmcr = 0;
    uint32_t phy = GMAC1_PHY_ADDR;

    switch (speedMbps) {
    case 10:
        break;                              /* speed bit clear */
    case 100:
        bmcr |= PHY_BMCR_SPD100;
        break;
    default:
        return -1;                          /* incl. 1000: cannot force */
    }
    if (fullDuplex != 0) {
        bmcr |= PHY_BMCR_FDUP;
    }
    return NetMdioWrite(phy, 0 /* BMCR */, bmcr);
}

/* ------------------------------------------------------------------ */
/* RX ring maintenance                                                */
/* ------------------------------------------------------------------ */

static void NetRxArm(uint32_t slot)
{
    g_rxDesc[slot].d0 = (uint32_t)(uintptr_t)g_rxPool[slot];
    g_rxDesc[slot].d1 = 0;
    g_rxDesc[slot].d2 = 0;
    NetDsb();
    g_rxDesc[slot].d3 = RXD3_OWN | RXD3_IOC | RXD3_BUF1V;
    g_rxState[slot] = SLOT_FREE;
}

static void NetRxKick(void)
{
    /* full-ring tail: the DMA may walk the whole ring and wraps on its
     * own; descriptors still owned by software simply backpressure it. */
    NetDsb();
    NetRegWrite(DMA_CH_RX_TAIL,
                 (uint32_t)(uintptr_t)&g_rxDesc[N_DESC - 1]);
}

/* reap completed RX descriptors into the ready queue (bounded by ring
 * size). called from the ISR and from NetRecv in polling mode. */
static void NetRxReap(void)
{
    uint32_t processed = 0;

    while (processed < N_DESC) {
        uint32_t slot = g_rxCur;
        uint32_t d3 = g_rxDesc[slot].d3;
        uint32_t queued = 0;

        if ((d3 & RXD3_OWN) != 0) {
            break;                       /* DMA still owns it: nothing new */
        }
        if (g_rxState[slot] != SLOT_FREE) {
            break;                       /* wrap onto a queued frame */
        }

        if ((d3 & RXD3_CTXT) != 0) {
            /* context (timestamp/PTR) descriptor: no frame */
            g_dev.stats.rxDropped++;
        } else if (((d3 & (RXD3_FD | RXD3_LD)) != (RXD3_FD | RXD3_LD)) ||
                   ((d3 & RXD3_ERR_SUM) != 0)) {
            /* split frame (RBSZ > MTU, unexpected) or errored frame */
            g_dev.stats.rxErrors++;
        } else {
            uint32_t len = d3 & RXD3_LEN_MASK;
            if ((len < 14u) || (len > FRAME_SIZE)) {
                g_dev.stats.rxDropped++;
            } else if (((g_rdyTail + 1u) % N_DESC) == g_rdyHead) {
                g_dev.stats.rxOverflow++;
                g_dev.stats.rxDropped++;
            } else {
                /* keep the descriptor software-owned: the frame data must
                 * survive until recv() copies it out (recv arms after the
                 * copy; fixed data-tearing window 2026-09-27). */
                g_rxLen[slot] = (uint16_t)len;
                g_rxState[slot] = SLOT_READY;
                g_rdyQ[g_rdyTail] = (uint8_t)slot;
                g_rdyTail = (g_rdyTail + 1u) % N_DESC;
                queued = 1;
            }
        }

        if (queued == 0) {
            /* only recycle slots that did NOT enter the queue */
            NetRxArm(slot);
        }
        g_rxCur = (g_rxCur + 1u) % N_DESC;
        processed++;
    }

    if (processed != 0) {
        NetRxKick();                    /* release the window */
    }
}

/* ------------------------------------------------------------------ */
/* TX ring maintenance (lazy completion reaping, no TIE)              */
/* ------------------------------------------------------------------ */

/* reap cursor: index of the oldest SLOT_PENDING descriptor. kept
 * separate from g_tx_alloc (next free slot) because pending frames are
 * always *behind* the allocation cursor - starting the scan at g_tx_alloc
 * would stop on the first free slot and never reap anything. */
static uint32_t g_txReap;

static void NetTxReap(void)
{
    while (g_txState[g_txReap] == SLOT_PENDING) {
        if ((g_txDesc[g_txReap].d3 & TXD3_OWN) != 0) {
            break;                       /* DMA still owns it */
        }
        if ((g_txDesc[g_txReap].d3 & RXD3_ERR_SUM) != 0) {
            g_dev.stats.txErrors++;
        }
        g_dev.stats.txCompleted++;
        g_txState[g_txReap] = SLOT_FREE;
        g_txReap = (g_txReap + 1u) % N_DESC;
    }
}

/* ------------------------------------------------------------------ */
/* interrupt service (interrupt mode)                                 */
/* ------------------------------------------------------------------ */

static VOID NetIsr(VOID)
{
    uint32_t status = NetRegRead(DMA_CH_STATUS);
    uint32_t clr = status & (DMA_CH_RI | DMA_CH_RBU |
                             DMA_CH_FBE | DMA_CH_CDE |
                             DMA_CH_AIS | DMA_CH_NIS);

    /* W1C on the bits we handle */
    NetRegWrite(DMA_CH_STATUS, clr);

    g_dev.stats.irqCount++;

    if ((status & (DMA_CH_RI | DMA_CH_RBU)) != 0) {
        NetRxReap();
    }
    if ((status & DMA_CH_AIS) != 0) {
        if ((status & (DMA_CH_FBE | DMA_CH_CDE)) != 0) {
            g_dev.stats.dmaErrors++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* contract implementation                                            */
/* ------------------------------------------------------------------ */

int NetInit(const netConfig *cfg, netDevice **dev)
{
    uint32_t val, poll;
    static const uint8_t mac[6] = GMAC1_DEFAULT_MAC;

    if ((cfg == NULL) || (dev == NULL)) {
        return NET_EINVAL;
    }
    if (cfg->portId != NET_PORT_GMAC1_RK3568) {
        return NET_ENODEV;           /* unknown/not handed-over port */
    }
    if (cfg->mode > NET_MODE_HYBRID) {
        return NET_EINVAL;
    }
    if ((cfg->autoneg == 0) &&
        ((cfg->speedMbps != 10) && (cfg->speedMbps != 100))) {
        /* 1000BASE-t cannot be force-linked (IEEE 802.3 mandates autoneg) */
        return NET_EINVAL;
    }
    if (g_dev.state == NET_STATE_RUNNING) {
        return NET_EBUSY;           /* running: stop first */
    }
    /* OFFLINE or READY both reach here. READY = reconfigure with a new
     * netConfig (e.g. switch mode or link policy): init is a full
     * reprogram anyway - DMA soft reset + every register rewritten - so
     * re-entry from READY is side-effect free. */

    /* --- link policy: force the PHY before touching the MAC --- *
     * the master stops writing MDIO after probe() (its link work only
     * reads), so this write is race-free. */
    if (cfg->autoneg == 0) {
        if (NetPhyForce(cfg->speedMbps, cfg->fullDuplex) != 0) {
            PRINT_ERR("[net] PHY force %uM failed\n", cfg->speedMbps);
            return NET_EIO;
        }
    } else {
        /* restore autoneg ONLY if a previous init forced the PHY (BMCR
         * aneg bit cleared) - a restart on an already-aneg PHY would
         * bounce the link at every re-init. */
        uint16_t bmcr = 0;
        if (NetMdioRead(GMAC1_PHY_ADDR, 0 /* BMCR */, &bmcr) != 0) {
            PRINT_ERR("[net] PHY BMCR read failed\n");
            return NET_EIO;
        }
        if ((bmcr & PHY_BMCR_ANEG) == 0) {
            if (NetMdioWrite(GMAC1_PHY_ADDR, 0 /* BMCR */,
                              PHY_BMCR_ANEG | PHY_BMCR_ANEG_RESTART) != 0) {
                PRINT_ERR("[net] PHY autoneg restore failed\n");
                return NET_EIO;
            }
        }
    }

    /* --- DMA soft reset (bus + channel state machines) --- */
    NetRegWrite(DMA_BUS_CFG, NetRegRead(DMA_BUS_CFG) | DMA_BUS_CFG_SW_RESET);
    for (poll = 0; poll < 100000u; poll++) {
        if ((NetRegRead(DMA_BUS_CFG) & DMA_BUS_CFG_SW_RESET) == 0) {
            break;
        }
    }
    if (poll == 100000u) {
        PRINT_ERR("[net] DMA soft reset timeout\n");
        return NET_EIO;
    }

    /* --- AXI bus mode: 32-bit addressing, fixed burst, short bursts --- */
    NetRegWrite(DMA_SYS_BUS_CFG,
                 DMA_SYS_BUS_CFG_FB | DMA_SYS_BUS_BL4 | DMA_SYS_BUS_BL8 |
                 DMA_SYS_BUS_BL16 | DMA_SYS_BUS_BL32);

    /* --- MAC static config: speed class must track the PHY - RGMII runs
     *     125 mHz only for 1000M and 25/2.5 mHz for 100/10M, so forced
     *     100/10M requires MII mode (PS) with FES for 100M; autoneg keeps
     *     GMII (partner expected 1000M-capable). rx filter per config. */
    val = NetRegRead(MAC_CONFIG);
    val &= ~(MAC_CFG_PS | MAC_CFG_FES | MAC_CFG_TE | MAC_CFG_RE);
    val |= MAC_CFG_DM | MAC_CFG_JD | MAC_CFG_BE |
           MAC_CFG_DCRS;
    if (cfg->autoneg == 0) {
        if (cfg->speedMbps == 100) {
            val |= MAC_CFG_PS | MAC_CFG_FES;  /* MII, 100M */
        } else {
            val |= MAC_CFG_PS;                    /* MII, 10M */
        }
    }
    NetRegWrite(MAC_CONFIG, val);

    if (cfg->promisc != 0) {
        NetRegWrite(MAC_PKT_FILTER,
                     MAC_FLT_PR | MAC_FLT_PM |
                     MAC_FLT_HMC | MAC_FLT_PCF);
    } else {
        /* unicast -> MAC_ADDR0 filter, multicast pass-all, control in */
        NetRegWrite(MAC_PKT_FILTER,
                     MAC_FLT_PM | MAC_FLT_HMC |
                     MAC_FLT_PCF);
    }

    NetRegWrite(MAC_ADDR0_HIGH, ((uint32_t)mac[5] << 8) | mac[4] |
                  (1u << 31));  /* always valid */
    NetRegWrite(MAC_ADDR0_LOW, ((uint32_t)mac[3] << 24) | ((uint32_t)mac[2] << 16) |
                  ((uint32_t)mac[1] << 8) | mac[0]);

    /* --- MTL: single queue, store-and-forward both ways --- */
    NetRegWrite(MTL_OP_MODE, MTL_SCHALG_SP | MTL_RAA_WSP);
    NetRegWrite(MTL_RXQ_MAP0, 0);   /* RX queue 0 -> DMA channel 0 */

    val = NetRegRead(MTL_TXQ0_MODE);
    val |= MTL_QMODE_TXQEN | MTL_QMODE_TSF | MTL_QMODE_TTC_32;
    NetRegWrite(MTL_TXQ0_MODE, val);

    val = NetRegRead(MTL_RXQ0_MODE);
    val |= MTL_QMODE_RSF | MTL_QMODE_RTC_64;
    NetRegWrite(MTL_RXQ0_MODE, val);

    /* --- DMA channel 0: rings, buffer size, engines still stopped --- */
    NetRegWrite(DMA_CH_TX_LIST, (uint32_t)(uintptr_t)g_txDesc);
    NetRegWrite(DMA_CH_RX_LIST, (uint32_t)(uintptr_t)g_rxDesc);
    NetRegWrite(DMA_CH_TX_LEN, N_DESC - 1);
    NetRegWrite(DMA_CH_RX_LEN, N_DESC - 1);

    NetRegWrite(DMA_CH_TX_CTRL,
                 (GMAC1_DMA_PBL << DMA_CH_PBL_SHIFT) | DMA_CH_OSP);
    NetRegWrite(DMA_CH_RX_CTRL,
                 ((FRAME_SIZE << DMA_CH_RBSZ_SHIFT) & DMA_CH_RBSZ_MASK));

    /* --- build the RX ring, wipe the TX ring --- */
    for (uint32_t i = 0; i < N_DESC; i++) {
        NetRxArm(i);
        g_txDesc[i].d0 = 0;
        g_txDesc[i].d1 = 0;
        g_txDesc[i].d2 = 0;
        g_txDesc[i].d3 = 0;
        g_txState[i] = SLOT_FREE;
    }
    g_rxCur = 0;
    g_txAlloc = 0;
    g_txReap = 0;
    g_rdyHead = 0;
    g_rdyTail = 0;

    g_dev.portId = cfg->portId;
    g_dev.mode = cfg->mode;
    g_dev.autoneg = (cfg->autoneg != 0) ? 1u : 0u;
    g_dev.forceSpeedMbps = (cfg->autoneg == 0) ? cfg->speedMbps : 0;
    g_dev.forceFullDuplex = (cfg->autoneg == 0) ? ((cfg->fullDuplex != 0) ? 1u : 0u) : 0;
    g_dev.promisc = (cfg->promisc != 0) ? 1u : 0u;
    g_dev.state = NET_STATE_READY;

    *dev = &g_dev;
    return 0;
}

int NetStart(netDevice *dev)
{
    if ((dev == NULL) || (dev != &g_dev)) {
        return NET_EINVAL;
    }
    if (dev->state == NET_STATE_RUNNING) {
        return 0;                        /* idempotent */
    }
    if (dev->state != NET_STATE_READY) {
        return NET_EIO;               /* FAULT: needs stop first */
    }

    if (dev->mode != NET_MODE_POLL) {
        if (ArchHwiCreate((HWI_HANDLE_T)GMAC1_IRQ_INTID, GMAC1_IRQ_LOS_PRIO,
                          HWI_MODE_TRIGGER_LEVEL,
                          (HWI_PROC_FUNC)NetIsr, (HwiIrqParam *)0) != LOS_OK) {
            PRINT_ERR("[net] ArchHwiCreate(%d) failed\n", GMAC1_IRQ_INTID);
            return NET_EIO;
        }
    }

    /* clear any latched status, arm interrupts (masked out in poll mode:
     * the MAC stays silent on the wire and recv() drives everything) */
    NetRegWrite(DMA_CH_STATUS, 0xffffffffu);
    if (dev->mode != NET_MODE_POLL) {
        NetRegWrite(DMA_CH_INT_EN,
                     DMA_CH_INT_NIE | DMA_CH_INT_AIE |
                     DMA_CH_INT_RIE | DMA_CH_INT_RBUE |
                     DMA_CH_INT_FBE | DMA_CH_INT_CDE);
    } else {
        NetRegWrite(DMA_CH_INT_EN, 0);
    }

    /* engine bring-up order: tail pointer first, then the DMA engines,
     * then MAC RE/TE */
    NetRxKick();
    NetRegWrite(DMA_CH_RX_CTRL,
                 NetRegRead(DMA_CH_RX_CTRL) | DMA_CH_SR);
    NetRegWrite(MAC_CONFIG, NetRegRead(MAC_CONFIG) | MAC_CFG_RE);

    /* TX tail at base before starting the engine: with the open-interval
     * [cursor, tail) fetch semantics this arms an empty ring; every
     * NetSend doorbell then points at the next free slot and advances
     * the window. */
    NetRegWrite(DMA_CH_TX_TAIL, (uint32_t)(uintptr_t)g_txDesc);
    NetRegWrite(DMA_CH_TX_CTRL,
                 NetRegRead(DMA_CH_TX_CTRL) | DMA_CH_ST);
    NetRegWrite(MAC_CONFIG, NetRegRead(MAC_CONFIG) | MAC_CFG_TE);

    dev->state = NET_STATE_RUNNING;
    return 0;
}

int NetStop(netDevice *dev, uint32_t timeoutUs)
{
    if ((dev == NULL) || (dev != &g_dev)) {
        return NET_EINVAL;
    }
    if (dev->state != NET_STATE_RUNNING) {
        return 0;
    }

    /* mask everything first so the ISR cannot restart engines */
    NetRegWrite(DMA_CH_INT_EN, 0);
    NetRegWrite(DMA_CH_STATUS, 0xffffffffu);

    NetRegWrite(DMA_CH_TX_CTRL,
                 NetRegRead(DMA_CH_TX_CTRL) & ~DMA_CH_ST);
    NetRegWrite(DMA_CH_RX_CTRL,
                 NetRegRead(DMA_CH_RX_CTRL) & ~DMA_CH_SR);
    NetRegWrite(MAC_CONFIG,
                 NetRegRead(MAC_CONFIG) & ~(MAC_CFG_TE | MAC_CFG_RE));

    /* bounded quiesce: DMA_DBG_STATUS TS[3:0]/RS[7:4] both 0 */
    {
        /* ~30 loop iterations per microsecond on a ~1.5GHz A55 */
        uint32_t budget = (timeoutUs == 0) ? 1u : (timeoutUs * 30u);
        uint32_t quiet = 0;
        while (budget-- != 0) {
            uint32_t dbg = NetRegRead(DMA_DBG_STATUS);
            if (((dbg & 0xfu) == 0) && (((dbg >> 4) & 0xfu) == 0)) {
                quiet = 1;
                break;
            }
        }
        if (quiet == 0) {
            dev->state = NET_STATE_FAULT;
            PRINT_ERR("[net] Stop: DMA quiesce timeout\n");
            return NET_ETIMEDOUT;
        }
    }

    if (dev->mode != NET_MODE_POLL) {
        /* ArchHwiDelete masks the SPI at the GICD (per-core ownership) */
        (VOID)ArchHwiDelete((HWI_HANDLE_T)GMAC1_IRQ_INTID, (HwiIrqParam *)0);
    }

    /* reset the rings for the next start */
    for (uint32_t i = 0; i < N_DESC; i++) {
        NetRxArm(i);
        g_txDesc[i].d3 = 0;
        g_txState[i] = SLOT_FREE;
    }
    g_rxCur = 0;
    g_txAlloc = 0;
    g_txReap = 0;
    g_rdyHead = 0;
    g_rdyTail = 0;

    dev->state = NET_STATE_READY;
    return 0;
}

int NetSend(netDevice *dev, const uint8_t *frame, uint32_t len)
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
        uint32_t idx = (g_txAlloc + slot) % N_DESC;
        if (g_txState[idx] == SLOT_FREE) {
            g_txAlloc = idx;
            break;
        }
    }
    if (slot == N_DESC) {
        dev->stats.txBusy++;
        return NET_EAGAIN;
    }

    memcpy(g_txPool[g_txAlloc], frame, len);

    g_txDesc[g_txAlloc].d0 = (uint32_t)(uintptr_t)g_txPool[g_txAlloc];
    g_txDesc[g_txAlloc].d1 = 0;
    g_txDesc[g_txAlloc].d2 = len & TXD2_BUF1_LEN_MASK;
    NetDsb();
    g_txDesc[g_txAlloc].d3 = TXD3_OWN | TXD3_FD | TXD3_LD |
                               (len & TXD3_LEN_MASK);
    g_txState[g_txAlloc] = SLOT_PENDING;

    /* doorbell: the DMA fetches descriptors in [cursor, tail) - the slot
     * the tail points at is NOT fetched, so write the tail as the NEXT
     * free slot (pointing it at the armed one stalls the DMA, 2026-09-23). */
    g_txAlloc = (g_txAlloc + 1u) % N_DESC;
    NetRegWrite(DMA_CH_TX_TAIL,
                 (uint32_t)(uintptr_t)&g_txDesc[g_txAlloc]);

    dev->stats.txAccepted++;
    return 0;
}

int NetRecv(netDevice *dev, uint8_t *frame, uint32_t capacity, uint32_t *len)
{
    uint32_t slot, flen;

    if ((dev == NULL) || (dev != &g_dev) || (frame == NULL) || (len == NULL)) {
        return NET_EINVAL;
    }
    if (dev->state != NET_STATE_RUNNING) {
        return NET_EIO;
    }

    /* POLL: recv() is the only reaper. HYBRID: the ISR reaps ahead of us;
     * reaping here too is idempotent and backstops a dropped IRQ. */
    if (dev->mode != NET_MODE_IRQ) {
        NetRxReap();
    }

    while (g_rdyHead != g_rdyTail) {
        slot = g_rdyQ[g_rdyHead];
        /* NOTE: g_rx_state[slot] is NOT checked here - the slot was armed
         * back to SLOT_FREE right after being queued (the frame data lives
         * in g_rx_pool until recv copies it out). the SPSC ring itself
         * guarantees the index/len pair is consistent. */
        flen = g_rxLen[slot];
        if (flen > capacity) {
            /* contract: short buffer -> drop the frame and count it */
            g_rdyHead = (g_rdyHead + 1u) % N_DESC;
            NetRxArm(slot);
            NetRxKick();
            dev->stats.rxDropped++;
            return NET_EMSGSIZE;
        }

        memcpy(frame, g_rxPool[slot], flen);

        g_rdyHead = (g_rdyHead + 1u) % N_DESC;
        NetRxArm(slot);
        NetRxKick();

        dev->stats.rxDelivered++;
        *len = flen;
        return 0;
    }
    return NET_EAGAIN;
}

int NetGetLinkInfo(netDevice *dev, netLinkInfo *info)
{
    uint16_t bmsr = 0, bmcr = 0;
    uint32_t link, speed = 0, duplex = 1;
    int ok;

    if ((dev == NULL) || (dev != &g_dev) || (info == NULL)) {
        return NET_EINVAL;
    }

    /* BMSR link bit is latched-low: read twice so a fresh "up" sample
     * is not masked by a stale low latch (same discipline as the
     * master's link poll). */
    ok = NetMdioRead(GMAC1_PHY_ADDR, PHY_REG_BMSR, &bmsr);
    ok |= NetMdioRead(GMAC1_PHY_ADDR, PHY_REG_BMSR, &bmsr);
    ok |= NetMdioRead(GMAC1_PHY_ADDR, PHY_REG_BMCR, &bmcr);
    if (ok != 0) {
        memset(info, 0, sizeof(*info));
        return 0;                        /* PHY not answering: invalid snapshot */
    }
    link = ((bmsr & BMSR_LSTATUS) != 0) ? 1u : 0u;

    /* resolved speed/duplex (phylib semantics): forced (aneg off) reads
     * BMCR bits (aneg result regs are stale in this mode); autoneg reads
     * MII_STAT1000 bit11 = 1000FD, else ANLPAR 10/100 bits. */
    if ((bmcr & PHY_BMCR_ANEG) == 0) {
        speed = (bmcr & PHY_BMCR_SPD100) ? 100u : 10u;
        duplex = (bmcr & PHY_BMCR_FDUP) ? 1u : 0u;
    } else {
        uint16_t gbsr = 0, lpa = 0;

        (VOID)NetMdioRead(GMAC1_PHY_ADDR, PHY_REG_STAT1000, &gbsr);
        (VOID)NetMdioRead(GMAC1_PHY_ADDR, PHY_REG_LPA, &lpa);
        if ((gbsr & LPA_1000FULL) != 0) {
            speed = 1000; duplex = 1;      /* 1000M FD */
        } else if ((lpa & LPA_100FULL) != 0) {
            speed = 100; duplex = 1;       /* 100M FD */
        } else if ((lpa & LPA_100HALF) != 0) {
            speed = 100; duplex = 0;       /* 100M HD */
        } else if ((lpa & LPA_10FULL) != 0) {
            speed = 10; duplex = 1;        /* 10M FD */
        } else if ((lpa & LPA_10HALF) != 0) {
            speed = 10; duplex = 0;        /* 10M HD */
        }
    }

    /* forced link: the aneg result registers are meaningless (no aneg
     * result to read) - publish the forced policy */
    if (dev->autoneg == 0) {
        speed = dev->forceSpeedMbps;
        duplex = dev->forceFullDuplex;
    }

    info->valid = 1;
    info->linkUp = link;
    info->speedMbps = speed;
    info->fullDuplex = duplex;
    info->sampleTimeNs = LOS_TickCountGet() *
                            (1000000000ull / (uint64_t)LOSCFG_BASE_CORE_TICK_PER_SECOND);

    if (link != dev->lastLinkUp) {
        dev->linkChanges++;
        dev->lastLinkUp = link;
    }
    return 0;
}

int NetGetStats(netDevice *dev, netStats *stats)
{
    if ((dev == NULL) || (dev != &g_dev) || (stats == NULL)) {
        return NET_EINVAL;
    }

    NetTxReap();                        /* fold in lazy TX completions */
    *stats = dev->stats;
    stats->linkChanges = dev->linkChanges;
    return 0;
}

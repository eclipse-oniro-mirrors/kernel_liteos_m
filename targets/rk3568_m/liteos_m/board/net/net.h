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
 * net.h - protocol-agnostic real-time NIC contract for the AMP slave
 * core. any link-layer protocol (etherCAT, proprietary, raw ethernet)
 * runs on top of NetSend/NetRecv; netDevice is opaque (owned by
 * net_dwmac.c). the linux master (mcs_net.ko) owns clocks/resets/pinmux
 * and PHY bring-up and must be loaded BEFORE the slave touches the port
 * (board/DRIVER_GUIDE.md); the slave owns the data path, its interrupt
 * and the on-demand link query.
 * frames: complete ethernet frames (dst MAC first, FCS excluded),
 * 14..1514 bytes; sub-60B frames are padded by the MAC. send copies in,
 * recv copies out and recycles; neither call keeps caller pointers.
 * multi-port: every GMAC the master hands over is identified by portId
 * and gets its own device instance (registered by the board profile,
 * gmac1_profile.h today); see NET_PORT_* below.
 */

#ifndef _NET_H_
#define _NET_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct netDevice netDevice;

/* ------------------------------------------------------------------ */
/* port selection: one id per GMAC the master handed over             */
/* ------------------------------------------------------------------ */
#define NET_PORT_GMAC1_RK3568   1    /* toybrick/DAYU210 GMAC1 (real-time port) */

/* ------------------------------------------------------------------ */
/* rx/tx operating mode (netConfig.mode)                               */
/* ------------------------------------------------------------------ */
#define NET_MODE_IRQ            0    /* interrupt driven (default) */
#define NET_MODE_POLL           1    /* pure polling: no IRQ, recv() reaps */
#define NET_MODE_HYBRID         2    /* IRQ driven + recv() reaps as backstop */

/* ------------------------------------------------------------------ */
/* frame sizing (receive buffers)                                      */
/* ------------------------------------------------------------------ */
/* largest frame the driver will deliver: 1514 max (no FCS) rounded up
 * for alignment. buffers passed to NetRecv() must be at least this many
 * bytes to be able to receive any legal frame. */
#define NET_FRAME_SIZE          1536

/* ------------------------------------------------------------------ */
/* init-time configuration                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t portId;       /* NET_PORT_*: which handed-over GMAC to use */
    uint32_t mode;          /* NET_MODE_* (default IRQ when 0) */
    /* link speed policy.
     * autoneg=1 (default): PHY autonegotiates (master bring-up), result
     *   read by getLinkInfo(); 10/100/1000 supported.
     * autoneg=0: force speedMbps/fullDuplex via the PHY BMCR (the master
     *   never writes MDIO after probe, so race-free). 10/100 only -
     *   1000BASE-t cannot be forced (IEEE 802.3), returns -EINVAL.
     * NOTE: a forced link needs the partner forced too; an autoneg
     * partner parallel-detects and usually lands HALF duplex. */
    uint32_t autoneg;       /* 1=autoneg (default), 0=force */
    uint32_t speedMbps;    /* force only: 10 or 100 */
    uint32_t fullDuplex;   /* force only: 0/1 */
    /* receive filter.
     * promisc=1 (default): accept everything (etherCAT / sniffer semantics).
     * promisc=0: unicast filtered to the station MAC + broadcast/multicast. */
    uint32_t promisc;       /* 1=promiscuous (default), 0=MAC filtered */
} netConfig;

typedef struct {
    uint32_t linkUp;       /* 0/1 */
    uint32_t speedMbps;    /* negotiated (autoneg) or forced value */
    uint32_t fullDuplex;   /* 0/1 */
    uint32_t valid;         /* 1: snapshot sampled from the PHY (link_up==0 is still a valid state) */
    uint64_t sampleTimeNs;/* slave tick clock at the sample */
} netLinkInfo;

typedef struct {
    uint32_t txAccepted;   /* send() calls accepted */
    uint32_t txCompleted;  /* frames the DMA reported done */
    uint32_t txErrors;     /* TX write-back error summary */
    uint32_t txBusy;       /* send() rejected with -EAGAIN (pool exhausted) */
    uint32_t rxDelivered;  /* frames copied out via recv() */
    uint32_t rxDropped;    /* bad/oversized/queue-overflow frames dropped */
    uint32_t rxErrors;     /* RX write-back error summary */
    uint32_t rxOverflow;   /* ready queue full at ISR time */
    uint32_t linkChanges;  /* link transitions counted by getLinkInfo() */
    uint32_t dmaErrors;    /* FBE/CDE abnormal events */
    uint32_t irqCount;     /* INTID deliveries (interrupt mode) */
    uint32_t reserved[4];
} netStats;

/* POSIX errno values (the bare slave has no errno.h) */
#define NET_EAGAIN     (-11)
#define NET_EINVAL     (-22)
#define NET_EMSGSIZE   (-90)
#define NET_ENODEV     (-19)
#define NET_EBUSY      (-16)
#define NET_EIO        (-5)
#define NET_ETIMEDOUT  (-110)

int NetInit(const netConfig *cfg, netDevice **dev);
int NetStart(netDevice *dev);
int NetStop(netDevice *dev, uint32_t timeoutUs);
int NetSend(netDevice *dev, const uint8_t *frame, uint32_t len);
int NetRecv(netDevice *dev, uint8_t *frame, uint32_t capacity, uint32_t *len);
int NetGetLinkInfo(netDevice *dev, netLinkInfo *info);
int NetGetStats(netDevice *dev, netStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* _NET_H_ */

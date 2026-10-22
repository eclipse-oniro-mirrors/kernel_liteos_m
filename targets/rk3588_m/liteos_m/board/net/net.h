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
 * net.h - generic real-time NIC contract for the AMP slave core.
 *
 * Originally frozen against "驱动接口依赖.md" (ECAT, 7 x P0); generalized
 * 2026-09-24 into a protocol-agnostic frame-level driver: any link-layer
 * protocol (EtherCAT, proprietary, raw Ethernet experiments) runs on top.
 * The protocol layer (e.g. an SOEM port) links against this header only;
 * NetDevice is opaque and owned by the board driver (net_dwmac.c).
 *
 * v3 (2026-09-27): the whole public surface was renamed from the ECAT-era
 * "EcEth" / "EC_ETH_" prefix to "Net" / "NET_" (de-ECAT, per user request).
 * Semantics, memory model and frame contract are unchanged from v2 -
 * call sites only need mechanical renames.
 *
 * RK3588 port: same contract; NET_PORT_GMAC1_RK3588 selects the handed-over
 * GMAC1. The forced-link policy (autoneg=0) is NOT available on this port
 * (see NetConfig below).
 *
 * Frame contract: complete Ethernet frames (dst MAC first, FCS excluded),
 * 14..1514 bytes; sub-60B frames are padded by the MAC. Send copies the
 * frame into driver-owned storage; Recv copies out and recycles. Neither
 * call keeps caller pointers after returning.
 *
 * Multi-port: the master (Linux) may hand over more than one GMAC to the
 * slave; each handed-over port is identified by port_id and gets its own
 * device instance + shared control block. Ports are registered by the
 * board profile (gmac1_profile.h today); see NET_PORT_* below.
 */

#ifndef _NET_H_
#define _NET_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NetDevice NetDevice;

/* ------------------------------------------------------------------ */
/* Port selection: one id per GMAC the master handed over             */
/* ------------------------------------------------------------------ */
#define NET_PORT_GMAC1_RK3588   1    /* bq3588hm GMAC1 / ETH2 (real-time port) */

/* ------------------------------------------------------------------ */
/* Rx/Tx operating mode (NetConfig.mode)                               */
/* ------------------------------------------------------------------ */
#define NET_MODE_IRQ            0    /* interrupt driven (default) */
#define NET_MODE_POLL           1    /* pure polling: no IRQ, Recv() reaps */
#define NET_MODE_HYBRID         2    /* IRQ driven + Recv() reaps as backstop */

/* ------------------------------------------------------------------ */
/* Frame sizing (receive buffers)                                      */
/* ------------------------------------------------------------------ */
/* Largest frame the driver will deliver: 1514 max (no FCS) rounded up
 * for alignment. Buffers passed to NetRecv() must be at least this many
 * bytes to be able to receive any legal frame. */
#define NET_FRAME_SIZE          1536

/* ------------------------------------------------------------------ */
/* Init-time configuration                                            */
/* ------------------------------------------------------------------ */
typedef struct {
    uint32_t port_id;       /* NET_PORT_*: which handed-over GMAC to use */
    uint32_t mode;          /* NET_MODE_* (default IRQ when 0) */
    /* Link speed policy.
     * autoneg=1 (default): the master-configured link state is published
     *   via GetLinkInfo(). On this board the GMAC1 RGMII side lands on a
     *   YT9214 switch port that runs fixed 1000M full duplex - the RJ45
     *   copper side of the switch negotiates independently and the switch
     *   buffers between speeds - so the MAC always runs GMII/1000M and
     *   the published link info describes the copper side (or the fixed
     *   1000M fallback when the switch runs unmanaged).
     * autoneg=0 (forced): NOT supported on this port - there is no
     *   forceable PHY directly behind this MAC (returns -ENOTSUP). */
    uint32_t autoneg;       /* must be 1 (or 0 = default) on this port */
    uint32_t speed_mbps;    /* reserved (forced links unsupported) */
    uint32_t full_duplex;   /* reserved (forced links unsupported) */
    /* Receive filter.
     * promisc=1 (default): accept everything (EtherCAT / sniffer semantics).
     * promisc=0: unicast filtered to the station MAC + broadcast/multicast. */
    uint32_t promisc;       /* 1=promiscuous (default), 0=MAC filtered */
} NetConfig;

typedef struct {
    uint32_t link_up;       /* 0/1 */
    uint32_t speed_mbps;    /* published by the master (copper side / fixed) */
    uint32_t full_duplex;   /* 0/1 */
    uint32_t valid;         /* 1: snapshot sampled (link_up==0 is still a valid state) */
    uint64_t sample_time_ns;
} NetLinkInfo;

typedef struct {
    uint32_t tx_accepted;   /* Send() calls accepted */
    uint32_t tx_completed;  /* frames the DMA reported done */
    uint32_t tx_errors;     /* TX write-back error summary */
    uint32_t tx_busy;       /* Send() rejected with -EAGAIN (pool exhausted) */
    uint32_t rx_delivered;  /* frames copied out via Recv() */
    uint32_t rx_dropped;    /* bad/oversized/queue-overflow frames dropped */
    uint32_t rx_errors;     /* RX write-back error summary */
    uint32_t rx_overflow;   /* ready queue full at ISR time */
    uint32_t link_changes;  /* mirror of the shared control block counter */
    uint32_t dma_errors;    /* FBE/CDE abnormal events */
    uint32_t irq_count;     /* INTID deliveries (interrupt mode) */
    uint32_t reserved[4];
} NetStats;

/* POSIX errno values (the bare slave has no errno.h) */
#define NET_EAGAIN     (-11)
#define NET_EINVAL     (-22)
#define NET_EMSGSIZE   (-90)
#define NET_ENODEV     (-19)
#define NET_EBUSY      (-16)
#define NET_EIO        (-5)
#define NET_ETIMEDOUT  (-110)
#define NET_ENOTSUP    (-95)

int NetInit(const NetConfig *cfg, NetDevice **dev);
int NetStart(NetDevice *dev);
int NetStop(NetDevice *dev, uint32_t timeout_us);
int NetSend(NetDevice *dev, const uint8_t *frame, uint32_t len);
int NetRecv(NetDevice *dev, uint8_t *frame, uint32_t capacity, uint32_t *len);
int NetGetLinkInfo(NetDevice *dev, NetLinkInfo *info);
int NetGetStats(NetDevice *dev, NetStats *stats);

#ifdef __cplusplus
}
#endif

#endif /* _NET_H_ */

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
 * net_ctrl.h - shared control block between the Linux master (mcs_net.ko)
 * and the LiteOS-M slave (net_dwmac.c) for the RK3588 GMAC1 real-time port.
 *
 * Layout is a contract: any change bumps NET_CTRL_VERSION and must be
 * mirrored into the master-side copy (device/board/maccura/rk3588_amp/
 * net/net_ctrl.h).
 *
 * Location: last 4KB page of the 1MB MCS shared window
 *   0x74000000..0x740fefff  openamp vring / rpmsg buffers (owned by mcs_km)
 *   0x740ff000..0x740fffff  this control block (+ can_ctrl.h pages
 *                           0x740ff100/0x740ff200)
 */

#ifndef _NET_CTRL_H_
#define _NET_CTRL_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_CTRL_PHYS        0x740FF000UL
#define NET_CTRL_SIZE        0x1000

#define NET_CTRL_MAGIC       0x45434E31UL  /* "ECN1" */
#define NET_CTRL_VERSION     2

/* master_state / slave_state */
#define NET_STATE_OFFLINE    0
#define NET_STATE_READY      1
#define NET_STATE_RUNNING    2
#define NET_STATE_FAULT      3

struct net_ctrl {
    uint32_t magic;         /* NET_CTRL_MAGIC when master is up */
    uint32_t version;       /* NET_CTRL_VERSION */
    uint32_t master_state;  /* written by master (mcs_net.ko) */
    uint32_t phy_id;        /* PHYSID1<<16 | PHYSID2 (0 when no PHY on MDIO) */

    /* link snapshot, refreshed by the master 1s delayed work */
    uint32_t link_up;       /* 0/1 */
    uint32_t speed_mbps;    /* 1000 (fixed RGMII side / YT9214 copper) */
    uint32_t full_duplex;   /* 0/1 */
    uint32_t valid;         /* snapshot has been sampled at least once */
    uint64_t sample_time_ns;/* master ktime_get_boottime_ns() */
    uint32_t link_changes;  /* count of link transitions observed */

    uint32_t slave_state;   /* written by slave (Net runtime state) */

    /* v2: instance parameters, written by the master before READY, read
     * by the slave. One ctrl block serves whichever GMAC instance the
     * master selected (module param gmac=), so this same firmware runs
     * on gmac0 (fe1b0000, RTL8211F, INTID 259, phy 0x0) and gmac1
     * (fe1c0000, YT9214, INTID 266, phy 0x1d). Absent/zero values fall
     * back to the compiled-in gmac1 profile. */
    uint32_t mac_base;      /* MAC CSR base of the handed-over GMAC */
    uint32_t irq_intid;     /* slave macirq INTID for ArchHwiCreate */
    uint32_t phy_addr;      /* MDIO probe address (0xff = none) */
};                          /* 64B = 1 cache line exactly */

#ifdef __cplusplus
}
#endif

#endif /* _NET_CTRL_H_ */

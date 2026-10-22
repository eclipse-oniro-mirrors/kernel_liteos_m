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
 * gmac1_profile.h - board profile for the RK3588 GMAC1 real-time endpoint
 * (bq3588hm ETH2).
 *
 * Everything board-specific lives here; porting to another board means a
 * new profile file + master-side dts node. The public port selector macro
 * lives in net.h (NET_PORT_GMAC1_RK3588 == 1 == this profile).
 *
 * Hardware contract (set up by the master mcs_net.ko before the slave
 * touches GMAC1): the five GMAC clocks held on, SRST_A_GMAC1
 * released, RGMII pinmux + php_grf
 * interface select + sys_grf delays applied, PHY autoneg configured.
 * The interrupt is owned by the slave: NetStart registers INTID 266
 * (GIC SPI 234 + 32) via ArchHwiCreate, whose gic-v3 createIrq applies
 * the per-core ownership contract (Group1-NS, IROUTER to CPU7, trigger,
 * priority, enable); the master never touches it.
 *
 * The RJ45 behind this port lands on a YT9214 switch: the GMAC1 RGMII
 * side runs fixed 1000M full duplex regardless of the copper-side
 * speed (the switch buffers between ports). When the switch is
 * unmanaged (SPI-flash dumb strapping) it will not answer MDIO at all -
 * the driver tolerates that (net.h: no forced-link policy on this
 * port); when it answers, its address is 0x1d.
 */

#ifndef _GMAC1_PROFILE_H_
#define _GMAC1_PROFILE_H_

#include <stdint.h>
#include "net.h"    /* NET_FRAME_SIZE: public frame contract */

#ifdef __cplusplus
extern "C" {
#endif

/* Ethernet MAC IP (same register map as the RK3568 GMAC1: MAC 0x0000,
 * MDIO 0x0200, MTL 0x0c00, DMA 0x1000, chan0 0x1100) */
#define GMAC1_BASE              0xFE1C0000UL
#define GMAC1_REG_SIZE          0x10000UL

/* YT9214 switch on the GMAC1 MDIO bus (may be silent in dumb mode) */
#define GMAC1_PHY_ADDR          0x1du

/* GIC SPI 234 -> INTID 266, level-triggered; owned by the slave core
 * (the gic-v3 createIrq routes the SPI to the creating core's affinity). */
#define GMAC1_IRQ_INTID         266
/* LOS prio 0..31 (0 = highest); the gic-v3 port maps it to the 8-bit GIC
 * field as prio << 3, so 4 -> 0x20 (the historical master-side value). */
#define GMAC1_IRQ_LOS_PRIO      4

/* Pool geometry: ring of enhanced (16B) descriptors, frame buffers big
 * enough for max Ethernet frame + headroom. Zero dynamic allocation
 * (contract). The pools live in the dedicated Normal Non-Cacheable
 * window mapped by kernel/liteos_m/arch/arm64/los_mmu.c (the slave
 * D-cache is enabled, so in-image BSS is Normal-WB and NOT DMA-coherent
 * - the GMAC is not IO-coherent). The RK3588 slave port already maps
 * this window (0x76000000, 2MB, Normal-NC). */
#define NET_DESC_NUM         16
/* frame size contract = NET_FRAME_SIZE (net.h, public) */

/* DMA pool window: must match NET_DMA_BASE in los_mmu.c (one 2MB L2
 * block mapped Normal-NC). Identity-mapped, so PA==VA holds and the ring
 * addresses programmed into the DMA registers stay valid. Layout inside:
 * rx desc @ +0x0000, tx desc @ +0x1000, rx pool @ +0x2000, tx pool
 * right after it (16 * 1536 = 0x6000). */
#define NET_DMA_BASE         0x76000000UL
#define NET_DMA_SIZE         0x00200000UL
#define NET_DMA_RX_DESC_OFF  0x0000UL
#define NET_DMA_TX_DESC_OFF  0x1000UL
#define NET_DMA_RX_POOL_OFF  0x2000UL
#define NET_DMA_TX_POOL_OFF  (NET_DMA_RX_POOL_OFF + \
                                 (NET_DESC_NUM * NET_FRAME_SIZE))

/* DMA burst (PBL, in beats) - conservative value validated on the same
 * IP on RK3568 */
#define GMAC1_DMA_PBL           8

/* Default station address written to GMAC_ADDR0 (informational when the
 * port runs promiscuous; it is the unicast filter target otherwise). */
#define GMAC1_DEFAULT_MAC       { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 }

#ifdef __cplusplus
}
#endif

#endif /* _GMAC1_PROFILE_H_ */

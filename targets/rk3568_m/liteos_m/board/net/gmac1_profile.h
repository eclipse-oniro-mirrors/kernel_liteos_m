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
 * gmac1_profile.h - board profile for the RK3568 GMAC1 real-time endpoint.
 * everything board-specific lives here; porting to another board means a
 * new profile file + master-side dts node. the public port selector macro
 * lives in net.h (NET_PORT_GMAC1_RK3568 == 1 == this profile).
 * PRECONDITION (see board/DRIVER_GUIDE.md): the master mcs_net.ko must
 * be loaded BEFORE the slave touches GMAC1 - it holds the clocks on,
 * releases the CRU reset, applies the RGMII pinmux + GRF delays and
 * configures the PHY; its 1s link work also keeps the RGMII mac_speed
 * clock tracking the resolved rate, so the ko must STAY loaded. with
 * the ko absent the clocks are gated and any GMAC MMIO stalls the AXI
 * bus (core hang). the interrupt is owned by the slave: NetStart
 * registers INTID 64 via ArchHwiCreate, whose gic-v3 createIrq applies
 * the per-core ownership contract (group1-NS, IROUTER to CPU3, trigger,
 * priority, enable); the master never touches it.
 */

#ifndef _GMAC1_PROFILE_H_
#define _GMAC1_PROFILE_H_

#include <stdint.h>
#include "net.h"    /* NET_FRAME_SIZE: public frame contract */

#ifdef __cplusplus
extern "C" {
#endif

/* synopsys dwmac-4.20a controller (same IP as eth0) */
#define GMAC1_BASE              0xFE010000UL

/* PHY on the GMAC1 MDIO bus (rgmii_phy1 = mdio1 phy@0) */
#define GMAC1_PHY_ADDR          0u

/* GIC SPI 32 -> INTID 64, level-triggered; owned by the slave core (the
 * gic-v3 createIrq routes the SPI to the creating core's affinity). */
#define GMAC1_IRQ_INTID         64
/* LOS prio 0..31 (0 = highest); the gic-v3 port maps it to the 8-bit GIC
 * field as prio << 3, so 4 -> 0x20 (the historical master-side value). */
#define GMAC1_IRQ_LOS_PRIO      4

/* pool geometry: ring of enhanced (16B) descriptors, frame buffers big
 * enough for max ethernet frame + headroom. zero dynamic allocation
 * (contract). the pools live in the dedicated normal non-cacheable
 * window mapped by kernel/liteos_m/arch/arm64/los_mmu.c (the slave
 * d-cache is enabled since the normal-cache enablement, so in-image BSS
 * is normal-WB and NOT DMA-coherent - the GMAC is not IO-coherent). */
#define NET_DESC_NUM         16
/* frame size contract = NET_FRAME_SIZE (net.h, public) */

/* DMA pool window: must match NET_DMA_BASE in los_mmu.c (one 2MB L2
 * block inside the slave-private region tail, 0x77A00000..0x77BFFFFF);
 * block mapped normal-NC). identity-mapped, so PA==VA holds and the ring
 * addresses programmed into the DMA registers stay valid. layout inside:
 * rx desc @ +0x0000, tx desc @ +0x1000, rx pool @ +0x2000, tx pool
 * right after it (16 * 1536 = 0x6000). */
#define NET_DMA_BASE         0x77A00000UL
#define NET_DMA_SIZE         0x00200000UL
#define NET_DMA_RX_DESC_OFF  0x0000UL
#define NET_DMA_TX_DESC_OFF  0x1000UL
#define NET_DMA_RX_POOL_OFF  0x2000UL
#define NET_DMA_TX_POOL_OFF  (NET_DMA_RX_POOL_OFF + \
                                 (NET_DESC_NUM * NET_FRAME_SIZE))

/* DMA burst (PBL, in beats) - conservative value validated on this IP */
#define GMAC1_DMA_PBL           8

/* default station address written to GMAC_ADDR0 (informational when the
 * port runs promiscuous; it is the unicast filter target otherwise). */
#define GMAC1_DEFAULT_MAC       { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 }

#ifdef __cplusplus
}
#endif

#endif /* _GMAC1_PROFILE_H_ */

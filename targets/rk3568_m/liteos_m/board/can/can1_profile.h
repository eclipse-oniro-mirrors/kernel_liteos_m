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
 * can1_profile.h - board profile for the RK3568 CAN1 real-time endpoint.
 * everything board-specific lives here; porting to another CAN controller
 * means a new profile file + master-side dts node. the public port
 * selector macro lives in can.h (CAN_PORT_CAN1_RK3568 == 1 == this
 * profile).
 * PRECONDITION (see board/DRIVER_GUIDE.md): the master mcs_can.ko must be
 * loaded BEFORE the slave touches CAN1 - it holds both clocks on with
 * baudclk retargeted to CAN1_BAUDCLK_HZ, pulses the CRU resets and
 * applies the can1m1 pinmux. with the ko absent the clocks are gated and
 * any CAN MMIO stalls the AXI bus (core hang). the interrupt is owned by
 * the slave: CanStart registers INTID 34 via ArchHwiCreate, whose gic-v3
 * createIrq applies the per-core ownership contract (group1-NS, IROUTER
 * to CPU3, trigger, priority, enable); the master never touches it.
 */

#ifndef _CAN1_PROFILE_H_
#define _CAN1_PROFILE_H_

#include <stdint.h>
#include "can.h"    /* canBittiming: public timing contract */

#ifdef __cplusplus
extern "C" {
#endif

/* rockchip CAN 2.0 controller (rk356x-oh.dtsi can@fe580000) */
#define CAN1_BASE               0xFE580000UL

/* baud clock the master mcs_can.ko retargets CLK_CAN1 to (MCS_CAN_BAUDCLK_HZ);
 * the bit-timing solver consumes this value. */
#define CAN1_BAUDCLK_HZ         100000000UL

/* GIC SPI 2 -> INTID 34, level-triggered; owned by the slave core (the
 * gic-v3 createIrq routes the SPI to the creating core's affinity). */
#define CAN1_IRQ_INTID          34
/* LOS prio 0..31 (0 = highest); the gic-v3 port maps it to the 8-bit GIC
 * field as prio << 3 (same value the net port uses, 4 -> 0x20). */
#define CAN1_IRQ_LOS_PRIO       4

/* hardware fifo geometry (rk356x-oh.dtsi):
 *   tx-fifo-depth = 1  -> CanSend is a synchronous single-frame handshake
 *   rx-fifo-depth = 6  -> the ISR/recv drain empties up to 6 frames/pass
 */
#define CAN1_RX_FIFO_DEPTH      6

/* default bitrate when canConfig.bitrate == 0 and no raw timing given. */
#define CAN1_DEFAULT_BITRATE    500000u

#ifdef __cplusplus
}
#endif

#endif /* _CAN1_PROFILE_H_ */

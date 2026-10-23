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
 * can1_profile.h - board profile for the RK3588 CAN1 real-time endpoint.
 *
 * Everything board-specific lives here; porting to another CAN controller
 * means a new profile file + master-side dts node. The public port
 * selector macro lives in can.h (CAN_PORT_CAN1_RK3588 == 1 == this
 * profile).
 *
 * Hardware contract (set up by the master mcs_can.ko before the slave
 * touches CAN1): baudclk retargeted to 100MHz + both CAN clocks held
 * on, CRU resets pulsed
 * (SRST_CAN1 + SRST_P_CAN1), can1m1 pinmux applied. The true read-back
 * baudclk rate is published in the shared control block (can_ctrl.h) and
 * the bit-timing solver consumes THAT value (for both the arbitration
 * and the CAN-FD data phase). The interrupt is owned by the slave:
 * CanStart registers INTID 374 via ArchHwiCreate, whose gic-v3 createIrq
 * applies the per-core ownership contract (Group1-NS, IROUTER to CPU7,
 * trigger, priority, enable); the master never touches it.
 *
 * Pins (TRM): can1m0 = GPIO3_B5 (rx) / GPIO3_B6 (tx) func0 -
 * the m0 group collides with the GMAC1 rgmii pins (gmac1_rx_bus2/
 * gmac1_tx_bus2) which this board uses for the net port; can1m1 =
 * GPIO4_B2 (rx) / GPIO4_B3 (tx) func12 is the group the master dts node
 * selects. GPIO4_B3 is ALSO claimed by the &gmac0 snps,reset-gpio in
 * the stock product dts (an adapter-board copy error); a can1m1 pinmux
 * failure on the master side is therefore possible on some board files
 * and NON-FATAL by design - internal loopback never leaves the die.
 */

#ifndef _CAN1_PROFILE_H_
#define _CAN1_PROFILE_H_

#include <stdint.h>
#include "can.h"    /* CanBittiming: public timing contract */

#ifdef __cplusplus
extern "C" {
#endif

/* Rockchip CAN 2.0/FD controller, CAN1 @ 0xfea60000 */
#define CAN1_BASE               0xFEA60000UL
#define CAN1_REG_SIZE           0x1000UL

/* shared control block (one 0x100 page per port in the MCS window) */
#define CAN1_CTRL_PHYS          0x740FF100UL

/* GIC SPI 342 -> INTID 374, level-triggered; owned by the slave core (the
 * gic-v3 createIrq routes the SPI to the creating core's affinity). */
#define CAN1_IRQ_INTID          374
/* LOS prio 0..31 (0 = highest); the gic-v3 port maps it to the 8-bit GIC
 * field as prio << 3 (same value the net port uses, 4 -> 0x20). */
#define CAN1_IRQ_LOS_PRIO       4

/* Hardware fifo geometry (CAN1/CAN2 nodes):
 *   tx-fifo-depth = 1  -> CanSend is a synchronous single-frame handshake
 *   rx-fifo-depth = 6  -> the ISR/Recv drain empties up to 6 frames/pass
 */
#define CAN1_RX_FIFO_DEPTH      6

/* Default bitrate when CanConfig.bitrate == 0 and no raw timing given. */
#define CAN1_DEFAULT_BITRATE    500000u

/* Default CAN-FD data-phase bitrate when CanConfig.data_bitrate == 0 and
 * fd_mode = 1 (2 Mbps is the common arbitration-500k/data-2M profile and
 * safely below the TDC threshold; see the TDC note in can.h). */
#define CAN1_DEFAULT_DATA_BITRATE 2000000u

#ifdef __cplusplus
}
#endif

#endif /* _CAN1_PROFILE_H_ */

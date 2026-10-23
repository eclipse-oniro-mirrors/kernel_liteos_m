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
 * can2_profile.h - board profile for the RK3588 CAN2 real-time endpoint.
 *
 * Twin of can1_profile.h: same CAN 2.0/FD IP, different base/irq/ctrl
 * page. Platform facts (RK3588 TRM):
 *   CAN2 @ 0xfea70000, interrupts = GIC_SPI 343 (INTID 375), the two
 *   CAN clocks (baud + apb), SRST_CAN2 / SRST_P_CAN2,
 *   tx-fifo-depth 1, rx-fifo-depth 6.
 * Pin groups: can2m0 = GPIO3_B3 (rx) / GPIO3_B4 (tx) func0;
 *             can2m1 = GPIO0_D4 (rx) / GPIO0_D5 (tx) func10.
 * The master dts node selects the m1 group. GPIO0_D5 is ALSO claimed by
 * the goodix touchscreen rst-gpio in the stock product device
 * tree); a can2m1 pinmux failure on the master side is therefore
 * possible and NON-FATAL by design - internal loopback never leaves the
 * die (only a real external bus on these pins needs the conflict
 * resolved in the master dts).
 *
 * Handover contract (set up by the master mcs_can.ko before the slave
 * touches CAN2): baudclk retargeted to 100MHz + both clocks held on,
 * CRU resets pulsed, can2m1 pinmux applied. The true read-back baudclk
 * rate is published in the shared control block (can_ctrl.h) and the
 * bit-timing solver consumes THAT value. The interrupt is owned by the
 * slave: CanStart registers INTID 375 via ArchHwiCreate, whose gic-v3
 * createIrq applies the per-core ownership contract (Group1-NS, IROUTER
 * to CPU7, trigger, priority, enable); the master never touches it.
 */

#ifndef _CAN2_PROFILE_H_
#define _CAN2_PROFILE_H_

#include <stdint.h>
#include "can.h"    /* CanBittiming: public timing contract */

#ifdef __cplusplus
extern "C" {
#endif

/* Rockchip CAN 2.0/FD controller, CAN2 @ 0xfea70000 */
#define CAN2_BASE               0xFEA70000UL
#define CAN2_REG_SIZE           0x1000UL

/* shared control block (one 0x100 page per port in the MCS window) */
#define CAN2_CTRL_PHYS          0x740FF200UL

/* GIC SPI 343 -> INTID 375, level-triggered; owned by the slave core */
#define CAN2_IRQ_INTID          375
/* LOS prio 0..31 (0 = highest); same mapping as CAN1 (4 -> 0x20) */
#define CAN2_IRQ_LOS_PRIO       4

/* Hardware fifo geometry (same IP as CAN1):
 *   tx-fifo-depth = 1  -> CanSend is a synchronous single-frame handshake
 *   rx-fifo-depth = 6  -> the ISR/Recv drain empties up to 6 frames/pass
 */
#define CAN2_RX_FIFO_DEPTH      6

/* Default bitrate when CanConfig.bitrate == 0 and no raw timing given. */
#define CAN2_DEFAULT_BITRATE    500000u

/* Default CAN-FD data-phase bitrate (see can1_profile.h). */
#define CAN2_DEFAULT_DATA_BITRATE 2000000u

#ifdef __cplusplus
}
#endif

#endif /* _CAN2_PROFILE_H_ */

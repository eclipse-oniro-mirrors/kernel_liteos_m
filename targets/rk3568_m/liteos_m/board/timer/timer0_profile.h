/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * You may not use this file except in compliance with the License.
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
 * timer0_profile.h - board profile for the RK3568 TIMER1 channel used as
 * the slave's shared-free timer endpoint. everything board-specific
 * lives here; another SoC timer channel = a new profile + one g_ports[]
 * entry. the public port selector macro lives in timer.h
 * (TIMER_PORT_TIMER1_RK3568 == 1 == this profile).
 *
 * WHY TIMER1 AND NOT TIMER0: the oh rk3568 dtb carries timer@fe5f0000
 * (TIMER0, GIC SPI 109) and the master kernel probes it as its
 * clockevent/sched-clock - clk_timer0 enable=1 with consumer
 * timer@fe5f0000 in clk_summary. TIMER0 is therefore NOT shared-free:
 * the slave pulsing SRST_P_TIMER (the block-wide APB reset) or
 * programming TIMER0 registers corrupts the master timer and the board
 * watchdog-resets minutes later (observed twice). TIMER1..TIMER5 have
 * no dtb node and clk enable=0 - the master never touches them.
 *
 * clock/reset policy for TIMER1:
 *  - the gate write ORs in PCLK_TIMER (bit3, needed to reach the
 *    registers; the master keeps it enabled anyway) plus TIMER1 (bit5).
 *    gates are only ever SET by the slave - never cleared - so the
 *    master clock state is never disturbed.
 *  - the reset pulse touches SRST_TIMER1 ONLY (bit5, per-channel
 *    reset id 389). SRST_P_TIMER (bit3) resets the whole timer block
 *    APB domain including the master-owned TIMER0 and must never be
 *    pulsed by the slave.
 *
 * the interrupt is owned by the slave: TimerStart registers INTID 142
 * (GIC SPI 110, level-triggered) via ArchHwiCreate (per-core SPI
 * ownership, same contract as can/net).
 */

#ifndef _TIMER0_PROFILE_H_
#define _TIMER0_PROFILE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* rockchip rk3568 TIMER1 (second channel of the fe5f0000 block,
 * rk3288 compatible register layout, 0x20 stride) */
#define TIMER1_BASE              0xFE5F0020UL

/* counter clock: xin24m fixed parent (no mux setup needed) */
#define TIMER1_CLOCK_HZ          24000000UL

/* GIC SPI 110 -> INTID 142, level-triggered; owned by the slave core */
#define TIMER1_IRQ_INTID         142
/* LOS prio 0..31 (0 = highest); same value the can/net ports use */
#define TIMER1_IRQ_LOS_PRIO      4

/* CRU (main cru @ fdd20000 - NOT fdd00000, that one is the PMUCRU!):
 * the driver ORs in the PCLK_TIMER + TIMER1 gates and pulses only the
 * TIMER1 channel reset at init (write-mask high half word convention).
 *   CLKGATE_CON(32) = fdd20000 + 0x300 + 32*4 = fdd20380
 *     bit3 = PCLK_TIMER gate, bit5 = TIMER1 gate
 *     (on-target read: 0x87e7 - bit3/bit4 clear = pclk+timer0 on,
 *      bits5-9 set = timer1..5 gated by the master clk_disable_unused)
 *   SOFTRST_CON(24) = fdd20000 + 0x400 + 24*4 = fdd20460
 *     reset id N lives in SOFTRST_CON(N/16) bit N%16:
 *     bit5 = SRST_TIMER1 (id 389). SRST_P_TIMER (id 387, bit3) is the
 *     block-wide APB reset and MUST stay untouched (the master owns
 *     TIMER0 in this block).
 *     (verified against the running dtb /proc/device-tree
 *     clock-controller@fdd20000 + linux clk-rk3568.c + dt-bindings
 *     rk3568-cru.h) */
#define TIMER1_CRU_BASE          0xFDD20000UL
#define TIMER1_CRU_GATE_REG      (TIMER1_CRU_BASE + 0x300UL + 32 * 4)
#define TIMER1_CRU_GATE_BITS     0x28UL            /* bit3 | bit5 */
#define TIMER1_CRU_RST_REG       (TIMER1_CRU_BASE + 0x400UL + 24 * 4)
#define TIMER1_CRU_RST_BITS      0x20UL            /* bit5 only */

#ifdef __cplusplus
}
#endif

#endif /* _TIMER0_PROFILE_H_ */

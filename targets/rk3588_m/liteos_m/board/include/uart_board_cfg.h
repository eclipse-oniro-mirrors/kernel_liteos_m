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
 * Board-level UART wiring for the RK3588 AMP slave (rk3588_m).
 *
 * The ONLY file a new RK3588 board should need to adapt: pick which
 * ports are wired out (BOARD_UARTn_EN, 0 = uart_init returns
 * UART_ERR_NOT_SUP for that port), which pinmux group each port uses
 * (BOARD_UARTn_PINMUX - the m0/m1(/m2) variants come from
 * uart_soc_rk3588.h), and the optional RS485 auto-direction echo
 * suppression (BOARD_UARTn_RS485_ECHO). Driver logic (board/uart.c)
 * stays untouched; a new board copies this file and edits the picks.
 *
 * The selections below follow the on-board survey of the BQ3588HM V3
 * (2026-10-24, master debugfs pinmux-pins + IOC readback):
 *   - UART2 m0 (GPIO0_B5/B6) is the shared debug console (fiq-debugger,
 *     boot-chain muxed, PMU_IOC domain);
 *   - UART5 m1 (GPIO3_C4/C5) and UART9 m2 (GPIO3_D4/D5) are wired and
 *     enabled in the vendor dts with no consumer - free for the slave
 *     (primary / secondary external-loopback ports);
 *   - UART6 m1 (GPIO1_A0..A3 + flow control) carries wireless-bluetooth
 *     on this board - disabled here; re-enable only after the master BT
 *     is shut down;
 *   - UART1/3/4 route to the baseboard P1/P2/P3 RS232 terminals and are
 *     free (vendor dts disabled); UART7 is board-reserved (see below);
 *     UART8 has no routing on this baseboard;
 *   - The baseboard terminal labelled "P6/UART6" is physically wired to
 *     the SoC UART9 pads (net names UART6_TX/RX sit on GPIO3_D4/D5 =
 *     uart9 m2) - UART9 is that terminal.
 *
 * DEPLOYMENT PRECONDITION (unchanged from the rk3568_m port): the
 * physical availability of the selected pins is owned by the
 * master-side integrator - the master core only passes a port id whose
 * pins are free on the target board, and this driver then owns the
 * full bring-up (clock + pinmux + GIC) of that port.
 */

#ifndef UART_BOARD_CFG_H
#define UART_BOARD_CFG_H

#include "uart_soc_rk3588.h"

/* ---- UART1: m1 (GPIO1_B7/B6 func10). No m0 on RK3588; m2 lives in the
 *      PMU_IOC domain (GPIO0_D2/D1) and is not writable from here. ---- */
#define BOARD_UART1_EN          1
#define BOARD_UART1_PINMUX      RK3588_UART1_PINMUX_M1
#define BOARD_UART1_RS485_ECHO  0

/* ---- UART2: console-shared with the Linux master (ttyFIQ0). m0 lives
 *      in the PMU_IOC domain (GPIO0_B5/B6) and is already muxed by the
 *      boot chain - no IOC write. m1 (GPIO4_D1/D0) / m2 (GPIO3_B2/B1)
 *      available for boards that route the console elsewhere. ---- */
#define BOARD_UART2_EN          1
#define BOARD_UART2_PINMUX      RK3588_UART2_PINMUX_M0_BOOTCHAIN
#define BOARD_UART2_RS485_ECHO  0

/* ---- UART3: m2 (GPIO4_A5/A6) - BQ3588HM wiring (balls carry the
 *      UART3_TX_M2/UART3_RX_M2 nets to the P2 RS232 terminal). The m0
 *      group (GPIO1_C0/C1) is taken by the audio circuits (I2C3_MIC_SDA /
 *      I2S0_MCLK) and m1 (GPIO3_B5/B6) by GMAC1_TXEN / USBCC_INT - do
 *      not pick those on this board. ---- */
#define BOARD_UART3_EN          1
#define BOARD_UART3_PINMUX      RK3588_UART3_PINMUX_M2
#define BOARD_UART3_RS485_ECHO  0

/* ---- UART4: m1 (GPIO3_D0/D1) - BQ3588HM wiring (balls carry the
 *      UART4_RX/UART4_TX nets to the P3 RS232 terminal). The m0 group
 *      (GPIO1_D2/D3) is taken by FLASH_SPI_CS / I2S0_SDI0 - do not pick
 *      it on this board. ---- */
#define BOARD_UART4_EN          1
#define BOARD_UART4_PINMUX      RK3588_UART4_PINMUX_M1
#define BOARD_UART4_RS485_ECHO  0

/* ---- UART5: m1 (GPIO3_C5/C4) - BQ3588HM wired (vendor dts enabled,
 *      pins verified muxed on the running master); primary external
 *      loopback port. m0 (GPIO4_D4/D5) / m2 (GPIO2_D4/D5) exist too. ---- */
#define BOARD_UART5_EN          1
#define BOARD_UART5_PINMUX      RK3588_UART5_PINMUX_M1
#define BOARD_UART5_RS485_ECHO  0

/* ---- UART6: BT-occupied on the BQ3588HM (wireless-bluetooth owns m1
 *      xfer+rtsn/ctsn on GPIO1_A0..A3). Disabled: re-enable only after
 *      the master-side BT is shut down. m2 (GPIO1_D1/D0) also exists. ---- */
#define BOARD_UART6_EN          0
#define BOARD_UART6_PINMUX      RK3588_UART6_PINMUX_M1
#define BOARD_UART6_RS485_ECHO  0

/* ---- UART7: board-reserved on the BQ3588HM, disabled. m2 (GPIO1_B4/B5)
 *      is wired to the 4G module (4G_UART7_TX/RX via R9910/R9914); m1
 *      (GPIO3_C0/C1) collides with ETH2_RGMII_INTN/PME (and the rtsn/ctsn
 *      pins C2/C3 with GMAC1 MDC/MDIO) - muxing either group breaks the
 *      ethernet/4G functions. Re-enable only after freeing the pins. ---- */
#define BOARD_UART7_EN          0
#define BOARD_UART7_PINMUX      RK3588_UART7_PINMUX_M2
#define BOARD_UART7_RS485_ECHO  0

/* ---- UART8: m0 (GPIO4_B1/B0); m1 (GPIO3_A3/A2) ---- */
#define BOARD_UART8_EN          1
#define BOARD_UART8_PINMUX      RK3588_UART8_PINMUX_M0
#define BOARD_UART8_RS485_ECHO  0

/* ---- UART9: m2 (GPIO3_D4/D5) - BQ3588HM wired to the baseboard P6 RS232
 *      terminal (the terminal nets are named "UART6" but sit on the uart9
 *      m2 pads); vendor dts enabled with no consumer. m1 (GPIO4_B5/B4)
 *      also exists. ---- */
#define BOARD_UART9_EN          1
#define BOARD_UART9_PINMUX      RK3588_UART9_PINMUX_M2
#define BOARD_UART9_RS485_ECHO  0

#endif /* UART_BOARD_CFG_H */

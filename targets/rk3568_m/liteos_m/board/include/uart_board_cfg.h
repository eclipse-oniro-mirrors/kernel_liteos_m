/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Board-level UART wiring for DAYU200 (RK3568).
 *
 * The ONLY file a new RK3568 board should need to adapt: pick which
 * ports are wired out (BOARD_UARTn_EN, 0 = uart_init returns
 * UART_ERR_NOT_SUP for that port), which pinmux group each port uses
 * (BOARD_UARTn_PINMUX - the m0/m1(/m2) variants come from
 * uart_soc_rk3568.h), and the optional RS485 auto-direction echo
 * suppression (BOARD_UARTn_RS485_ECHO). Driver logic (board/uart.c)
 * stays untouched; a new board copies this file and edits the picks.
 *
 * The selections below reproduce the original DAYU200 profile 1:1
 * (《Modbus-RTU_UART驱动实现方案_rk3568.md》V0.17 §6).
 *
 * DEPLOYMENT PRECONDITION: the physical availability of the selected
 * pins is owned by the master-side integrator - the master core only
 * passes a port id whose pins are free on the target board, and this
 * driver then owns the full bring-up (clock + pinmux + GIC) of that
 * port. Reference data measured on THIS DAYU200 bench (Linux master
 * device tree, 2026-09-28, see the verification report) for the master
 * side when picking ports:
 *   UART1 m1 (GPIO3_D6/D7)  - GMAC1 rgmii-bus (in use)
 *   UART2 m0 (GPIO0_D0/D1)  - master console (ttyFIQ0)
 *   UART3 m0 (GPIO1_A0/A1)  - i2c3 (master UART3 node uses m1)
 *   UART4 m1 (GPIO3_B1/B2)  - master uart0-xfer group
 *   UART5 m0 (GPIO2_A1/A2)  - sdmmc0 (SD card cmd/clk)
 *   UART6 m0 (GPIO2_A3/A4)  - GMAC0 rgmii-clk
 *   UART6 m1 (GPIO1_D5/D6)  - sdmmc2 (eMMC clk/cmd - root fs!)
 *   UART7 m0 (GPIO2_A5/A6)  - i2c5 (A5) + GMAC0 rgmii-bus (A6)
 *   UART8 m0 (GPIO2_C5/C6)  - i2s2-sdi (C5) + BT (node okay)
 *   UART9 m0 (GPIO2_A7/B0)  - GMAC0 (A7) + i2c5 (B0)
 *   UART9 m1 (GPIO4_C5/C6)  - no reference found in the master tree:
 *                             the only pin pair potentially free here.
 */

#ifndef UART_BOARD_CFG_H
#define UART_BOARD_CFG_H

#include "uart_soc_rk3568.h"

/* ---- UART1: m1 (GPIO3_D6/D7 func4) - BOTH m0/m1 conflict with the
 *      dual GbE on DAYU200, data kept for other boards ---- */
#define BOARD_UART1_EN          1
#define BOARD_UART1_PINMUX      RK3568_UART1_PINMUX_M1
#define BOARD_UART1_RS485_ECHO  0

/* ---- UART2: console-shared with the Linux master (ttyFIQ0). m0 lives
 *      in the PMUGRF domain and is already muxed by the boot chain - no
 *      GRF write. m1 (GPIO1_D5/D6 func2) collides with UART6 m1 pins. ---- */
#define BOARD_UART2_EN          1
#define BOARD_UART2_PINMUX      RK3568_UART2_PINMUX_M0_BOOTCHAIN
#define BOARD_UART2_RS485_ECHO  0

/* ---- UART3: m0 (GPIO1_A0/A1 func2) - pins currently held by I2C3 on
 *      DAYU200, data kept for other boards ---- */
#define BOARD_UART3_EN          1
#define BOARD_UART3_PINMUX      RK3568_UART3_PINMUX_M0
#define BOARD_UART3_RS485_ECHO  0

/* ---- UART4: WARNING (legacy). The original profile value kept below
 *      muxes GPIO1_A4(rx) + GPIO1_A7(uart4_ctsn!) func2 - the TX pin
 *      GPIO1_A6 is NOT muxed, contradicting mainline
 *      rk3568-pinctrl.dtsi (uart4m0_xfer = A4 rx / A6 tx). Preserved
 *      1:1 for behavior compatibility; the fix is a one-liner:
 *      #define BOARD_UART4_PINMUX  RK3568_UART4_PINMUX_M0 ---- */
#define BOARD_UART4_EN          1
#define BOARD_UART4_PINMUX_LEGACY_A7 \
    { { RK3568_GRF_IOMUX_H(1, 0), 0xF00F2002UL }, { 0, 0 } }
#define BOARD_UART4_PINMUX      BOARD_UART4_PINMUX_LEGACY_A7
#define BOARD_UART4_RS485_ECHO  0

/* ---- UART5: m0 (GPIO2_A1/A2 func3) - conflicts CAN0 m1 on DAYU200 ---- */
#define BOARD_UART5_EN          1
#define BOARD_UART5_PINMUX      RK3568_UART5_PINMUX_M0
#define BOARD_UART5_RS485_ECHO  0

/* ---- UART6: m1 (GPIO1_D5 TX / D6 RX func3) - V0.15 development test
 *      port (m0 = GPIO2_A3/A4 func3, spans two GRF registers) ---- */
#define BOARD_UART6_EN          1
#define BOARD_UART6_PINMUX      RK3568_UART6_PINMUX_M1
#define BOARD_UART6_RS485_ECHO  0

/* ---- UART7 ~ UART9: m0 ---- */
#define BOARD_UART7_EN          1
#define BOARD_UART7_PINMUX      RK3568_UART7_PINMUX_M0
#define BOARD_UART7_RS485_ECHO  0

#define BOARD_UART8_EN          1
#define BOARD_UART8_PINMUX      RK3568_UART8_PINMUX_M0
#define BOARD_UART8_RS485_ECHO  0

#define BOARD_UART9_EN          1
#define BOARD_UART9_PINMUX      RK3568_UART9_PINMUX_M0
#define BOARD_UART9_RS485_ECHO  0

#endif /* UART_BOARD_CFG_H */

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
 * RK3588 SoC-level UART hardware facts: MMIO bases, GIC interrupt ids,
 * IOC iomux registers, CRU clock select/gate registers and the per-port
 * pinmux variant set for UART1~UART9.
 *
 * SoC-constant: identical for every board built on RK3588. The board
 * wiring choices (which ports are enabled, which pinmux group each port
 * uses, RS485 echo suppression) live in uart_board_cfg.h.
 *
 * Every value is transcribed from the in-tree Linux sources
 * (device/soc/rockchip/rk3588/kernel) and cross-verified on the
 * BQ3588HM board (2026-10-24, read-only devmem anchors):
 *   - bases / interrupt ids: arch/arm64/boot/dts/rockchip/rk3588s.dtsi
 *     (uart1~9 = GIC_SPI 332~340, INTID = SPI + 32 = 364~372);
 *   - iomux: the pinctrl driver (linux 6.6 drivers/pinctrl/pinctrl-rockchip.c,
 *     rk3588_pin_banks + rockchip_set_mux RK3588 path) maps GPIO1~GPIO4
 *     iomux into BUS_IOC = ioc(0xFD5F0000) + 0x8000, with group registers
 *     walking 8 bytes per group CONTINUOUSLY across banks (GPIO1_A at
 *     +0x20 ... GPIO4_D at +0x98); L register covers pins 0..3, H covers
 *     pins 4..7, one 4-bit function field per pin, func 10 = uart.
 *     On-board anchors: GPIO1A_L=0x0000AAAA (uart6 m1 xfer+rtsn/ctsn),
 *     GPIO3C_H=0x000000AA (uart5 m1), GPIO3D_H=0x000000AA (uart9 m2);
 *   - pinmux groups: arch/arm64/boot/dts/rockchip/rk3588s-pinctrl.dtsi;
 *   - CLKSEL_CON / CLKGATE_CON numbers and gate bits:
 *     drivers/clk/rockchip/clk-rk3588.c (CLKSEL_CON(n) = CRU + 0x300 +
 *     4n, CLKGATE_CON(n) = CRU + 0x800 + 4n), on-board gate readback
 *     CON(12)/CON(13)/CON(14) matches the running-master clock state.
 *
 * RK3588 clock-tree difference vs RK3568 (visible in this table):
 *   - the sclk_uartN gate bits are NOT slot-aligned with the pclk_uartN
 *     bits: pclk_uart1..9 all live in CLKGATE_CON(12) bits 2..10, while
 *     sclk_uart1..9 walk CON(12).13 .. CON(14).5. Each port therefore
 *     carries TWO gate registers (gateReg = pclk, gateReg2 = sclk;
 *     uart1 is the only single-register case).
 *   - the fracmux parent select sits at bits [1:0] of CLKSEL_CON(n)
 *     (parents { src, frac, xin24m }, index 2 = 24 MHz crystal).
 *
 * RK3588 iomux difference vs RK3568: there is no single sys_grf iomux
 * window - the pinmux registers live in the ioc block (0xFD5F0000,
 * "rockchip,rk3588-ioc") and GPIO1~GPIO4 use the BUS_IOC sub-window at
 * +0x8000. GPIO0 groups sit in the PMU domain (PMU1_IOC/PMU2_IOC proxy,
 * see the pinctrl driver) and are not writable from here: the UART0 and
 * UART1-m2/UART2-m0 groups that live there are boot-chain-owned and this
 * driver never writes them (profile entries carry reg = 0 = no write).
 */

#ifndef UART_SOC_RK3588_H
#define UART_SOC_RK3588_H

#include <stdint.h>

/* ===========================================================================
 * CRU / IOC register formulas (RK3588 TRM layout).
 * ========================================================================= */
#define RK3588_CRU_BASE                0xFD7C0000UL
#define RK3588_IOC_BASE                0xFD5F0000UL
#define RK3588_BUS_IOC_BASE            (RK3588_IOC_BASE + 0x8000UL)

/* CLKSEL_CON(n)  = CRU + 0x300 + 4n  (clock source select / divider) */
#define RK3588_CLKSEL_CON(n)           (RK3588_CRU_BASE + 0x300UL + 4UL * (n))
/* CLKGATE_CON(n) = CRU + 0x800 + 4n  (clock gates, SET_TO_DISABLE) */
#define RK3588_CLKGATE_CON(n)          (RK3588_CRU_BASE + 0x800UL + 4UL * (n))

/* GPIO1~GPIO4 iomux group register (BUS_IOC window): banks walk 8 bytes
 * per group CONTINUOUSLY (GPIO1_A at +0x20, GPIO2_A at +0x40, GPIO3_A at
 * +0x60, GPIO4_A at +0x80; each group takes 8 bytes: L register = pins
 * 0..3, H register = pins 4..7, one 4-bit function field per pin). */
#define RK3588_IOMUX_L(bank, grp)      (RK3588_BUS_IOC_BASE + 0x20UL * (bank) + 8UL * (grp))
#define RK3588_IOMUX_H(bank, grp)      (RK3588_IOMUX_L(bank, grp) + 4UL)

/* HIWORD16 write convention ([31:16] mask, [15:0] data): only the masked
 * bits of the target register change - no read-modify-write races with
 * the Linux master core sharing the CRU/IOC. */
#define RK3588_HIWORD16(mask, data)    (((uint32_t)(mask) << 16) | (uint32_t)(data))

/* ===========================================================================
 * Shared per-port clock values.
 * ========================================================================= */
/* sclk_uartN fracmux parent field [1:0]; the parent table in clk-rk3588.c
 * is { clk_uartN_src, clk_uartN_frac, xin24m } -> index 2 = 24 MHz
 * crystal. Independent of the PLLs the Linux master may re-rate at any
 * time (AMP). Defensive: the boot chain already selects it. */
#define RK3588_UART_CLKSEL_VAL_XIN24M  RK3588_HIWORD16(0x3, 0x2)

/* Clock gates are SET_TO_DISABLE (write 0 = enable). Unlike RK3568 the
 * pclk and sclk gates of one UART sit in different CLKGATE_CON registers
 * (see the file header): the profile carries gateReg (pclk, CON(12)) and
 * gateReg2 (sclk, CON(13)/CON(14)); uart1 is the only same-register case
 * and folds both bits into gateReg with gateReg2 = 0 (unused). The src
 * and frac path gates stay closed - the mux feeds sclk directly from
 * xin24m. */
#define RK3588_UART_GATE_VAL(bit)      RK3588_HIWORD16(1UL << (bit), 0)

/* pclk_uart1..9: CLKGATE_CON(12) bits 2..10 */
#define RK3588_UART_PCLK_GATE_REG      RK3588_CLKGATE_CON(12)
#define RK3588_UART_PCLK_GATE_VAL(n)   RK3588_UART_GATE_VAL(1UL * ((n) + 1))

/* ===========================================================================
 * Per-port facts: UART1 ~ UART9.
 * UART0 lives in the PMU/PMUCRU domain and is not supported by this
 * driver; RK3588_UART_PORT_MAX bounds the logical port id space.
 * ========================================================================= */
#define RK3588_UART_PORT_MIN           1U
#define RK3588_UART_PORT_MAX           9U

/* ---- UART1: GIC SPI 332, mux CLKSEL_CON(43), pclk CON(12).2, sclk
 *      CON(12).13 (single-register case: 0x2004 opens both) ---- */
#define RK3588_UART1_BASE        0xFEB40000UL
#define RK3588_UART1_INT_ID      364
#define RK3588_UART1_CLKSEL_REG  RK3588_CLKSEL_CON(43)
#define RK3588_UART1_GATE_REG    RK3588_CLKGATE_CON(12)
#define RK3588_UART1_GATE_VAL    RK3588_HIWORD16(0x2004, 0) /* pclk b2 + sclk b13 */
#define RK3588_UART1_GATE2_REG   0UL /* unused: both gates in GATE_REG */
#define RK3588_UART1_GATE2_VAL   0
#define RK3588_UART1_PINMUX_M1   { { RK3588_IOMUX_H(1, 1), 0xFF00AA00UL }, \
                                   { 0, 0 } } /* GPIO1_B7(rx)/B6(tx) func10 */
#define RK3588_UART1_PINMUX_M2   { { 0, 0 }, { 0, 0 } } /* GPIO0_D2/D1 func10:
                              PMU_IOC domain - mux by the boot chain, no write */

/* ---- UART2: GIC SPI 333, mux CLKSEL_CON(45), pclk CON(12).3, sclk
 *      CON(13).0 ---- */
#define RK3588_UART2_BASE        0xFEB50000UL
#define RK3588_UART2_INT_ID      365
#define RK3588_UART2_CLKSEL_REG  RK3588_CLKSEL_CON(45)
#define RK3588_UART2_GATE_REG    RK3588_UART_PCLK_GATE_REG
#define RK3588_UART2_GATE_VAL    RK3588_UART_PCLK_GATE_VAL(2)
#define RK3588_UART2_GATE2_REG   RK3588_CLKGATE_CON(13)
#define RK3588_UART2_GATE2_VAL   RK3588_UART_GATE_VAL(0)
#define RK3588_UART2_PINMUX_M0_BOOTCHAIN \
    { { 0, 0 }, { 0, 0 } } /* GPIO0_B6(rx)/B5(tx) func10: PMU_IOC domain,
                              already muxed by the boot chain - no write */
#define RK3588_UART2_PINMUX_M1   { { RK3588_IOMUX_L(4, 3), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO4_D1(rx)/D0(tx) func10 */
#define RK3588_UART2_PINMUX_M2   { { RK3588_IOMUX_L(3, 1), 0x0FF00AA0UL }, \
                                   { 0, 0 } } /* GPIO3_B2(rx)/B1(tx) func10 */

/* ---- UART3: GIC SPI 334, mux CLKSEL_CON(47), pclk CON(12).4, sclk
 *      CON(13).3 ---- */
#define RK3588_UART3_BASE        0xFEB60000UL
#define RK3588_UART3_INT_ID      366
#define RK3588_UART3_CLKSEL_REG  RK3588_CLKSEL_CON(47)
#define RK3588_UART3_GATE_REG    RK3588_UART_PCLK_GATE_REG
#define RK3588_UART3_GATE_VAL    RK3588_UART_PCLK_GATE_VAL(3)
#define RK3588_UART3_GATE2_REG   RK3588_CLKGATE_CON(13)
#define RK3588_UART3_GATE2_VAL   RK3588_UART_GATE_VAL(3)
#define RK3588_UART3_PINMUX_M0   { { RK3588_IOMUX_L(1, 2), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO1_C0(rx)/C1(tx) func10 */
#define RK3588_UART3_PINMUX_M1   { { RK3588_IOMUX_H(3, 1), 0x0FF00AA0UL }, \
                                   { 0, 0 } } /* GPIO3_B6(rx)/B5(tx) func10 */
#define RK3588_UART3_PINMUX_M2   { { RK3588_IOMUX_H(4, 0), 0x0FF00AA0UL }, \
                                   { 0, 0 } } /* GPIO4_A6(rx)/A5(tx) func10 */

/* ---- UART4: GIC SPI 335, mux CLKSEL_CON(49), pclk CON(12).5, sclk
 *      CON(13).6 ---- */
#define RK3588_UART4_BASE        0xFEB70000UL
#define RK3588_UART4_INT_ID      367
#define RK3588_UART4_CLKSEL_REG  RK3588_CLKSEL_CON(49)
#define RK3588_UART4_GATE_REG    RK3588_UART_PCLK_GATE_REG
#define RK3588_UART4_GATE_VAL    RK3588_UART_PCLK_GATE_VAL(4)
#define RK3588_UART4_GATE2_REG   RK3588_CLKGATE_CON(13)
#define RK3588_UART4_GATE2_VAL   RK3588_UART_GATE_VAL(6)
#define RK3588_UART4_PINMUX_M0   { { RK3588_IOMUX_L(1, 3), 0xFF00AA00UL }, \
                                   { 0, 0 } } /* GPIO1_D3(rx)/D2(tx) func10 */
#define RK3588_UART4_PINMUX_M1   { { RK3588_IOMUX_L(3, 3), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO3_D0(rx)/D1(tx) func10 */
#define RK3588_UART4_PINMUX_M2   { { RK3588_IOMUX_L(1, 1), 0xFF00AA00UL }, \
                                   { 0, 0 } } /* GPIO1_B2(rx)/B3(tx) func10 */

/* ---- UART5: GIC SPI 336, mux CLKSEL_CON(51), pclk CON(12).6, sclk
 *      CON(13).9 ---- */
#define RK3588_UART5_BASE        0xFEB80000UL
#define RK3588_UART5_INT_ID      368
#define RK3588_UART5_CLKSEL_REG  RK3588_CLKSEL_CON(51)
#define RK3588_UART5_GATE_REG    RK3588_UART_PCLK_GATE_REG
#define RK3588_UART5_GATE_VAL    RK3588_UART_PCLK_GATE_VAL(5)
#define RK3588_UART5_GATE2_REG   RK3588_CLKGATE_CON(13)
#define RK3588_UART5_GATE2_VAL   RK3588_UART_GATE_VAL(9)
#define RK3588_UART5_PINMUX_M0   { { RK3588_IOMUX_H(4, 3), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO4_D4(rx)/D5(tx) func10 */
#define RK3588_UART5_PINMUX_M1   { { RK3588_IOMUX_H(3, 2), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO3_C5(rx)/C4(tx) func10;
                              on-board anchor: reg reads 0x000000AA */
#define RK3588_UART5_PINMUX_M2   { { RK3588_IOMUX_H(2, 3), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO2_D4(rx)/D5(tx) func10 */

/* ---- UART6: GIC SPI 337, mux CLKSEL_CON(53), pclk CON(12).7, sclk
 *      CON(13).12 ---- */
#define RK3588_UART6_BASE        0xFEB90000UL
#define RK3588_UART6_INT_ID      369
#define RK3588_UART6_CLKSEL_REG  RK3588_CLKSEL_CON(53)
#define RK3588_UART6_GATE_REG    RK3588_UART_PCLK_GATE_REG
#define RK3588_UART6_GATE_VAL    RK3588_UART_PCLK_GATE_VAL(6)
#define RK3588_UART6_GATE2_REG   RK3588_CLKGATE_CON(13)
#define RK3588_UART6_GATE2_VAL   RK3588_UART_GATE_VAL(12)
#define RK3588_UART6_PINMUX_M1   { { RK3588_IOMUX_L(1, 0), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO1_A0(rx)/A1(tx) func10;
                              on-board anchor: reg reads 0x0000AAAA (xfer
                              + rtsn/ctsn on A0..A3) */
#define RK3588_UART6_PINMUX_M2   { { RK3588_IOMUX_L(1, 3), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO1_D1(rx)/D0(tx) func10 */

/* ---- UART7: GIC SPI 338, mux CLKSEL_CON(55), pclk CON(12).8, sclk
 *      CON(13).15 ---- */
#define RK3588_UART7_BASE        0xFEBA0000UL
#define RK3588_UART7_INT_ID      370
#define RK3588_UART7_CLKSEL_REG  RK3588_CLKSEL_CON(55)
#define RK3588_UART7_GATE_REG    RK3588_UART_PCLK_GATE_REG
#define RK3588_UART7_GATE_VAL    RK3588_UART_PCLK_GATE_VAL(7)
#define RK3588_UART7_GATE2_REG   RK3588_CLKGATE_CON(13)
#define RK3588_UART7_GATE2_VAL   RK3588_UART_GATE_VAL(15)
#define RK3588_UART7_PINMUX_M1   { { RK3588_IOMUX_L(3, 2), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO3_C1(rx)/C0(tx) func10 */
#define RK3588_UART7_PINMUX_M2   { { RK3588_IOMUX_H(1, 1), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO1_B4(rx)/B5(tx) func10 */

/* ---- UART8: GIC SPI 339, mux CLKSEL_CON(57), pclk CON(12).9, sclk
 *      CON(14).2 ---- */
#define RK3588_UART8_BASE        0xFEBB0000UL
#define RK3588_UART8_INT_ID      371
#define RK3588_UART8_CLKSEL_REG  RK3588_CLKSEL_CON(57)
#define RK3588_UART8_GATE_REG    RK3588_UART_PCLK_GATE_REG
#define RK3588_UART8_GATE_VAL    RK3588_UART_PCLK_GATE_VAL(8)
#define RK3588_UART8_GATE2_REG   RK3588_CLKGATE_CON(14)
#define RK3588_UART8_GATE2_VAL   RK3588_UART_GATE_VAL(2)
#define RK3588_UART8_PINMUX_M0   { { RK3588_IOMUX_L(4, 1), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO4_B1(rx)/B0(tx) func10 */
#define RK3588_UART8_PINMUX_M1   { { RK3588_IOMUX_L(3, 0), 0xFF00AA00UL }, \
                                   { 0, 0 } } /* GPIO3_A3(rx)/A2(tx) func10 */

/* ---- UART9: GIC SPI 340, mux CLKSEL_CON(59), pclk CON(12).10, sclk
 *      CON(14).5 ---- */
#define RK3588_UART9_BASE        0xFEBC0000UL
#define RK3588_UART9_INT_ID      372
#define RK3588_UART9_CLKSEL_REG  RK3588_CLKSEL_CON(59)
#define RK3588_UART9_GATE_REG    RK3588_UART_PCLK_GATE_REG
#define RK3588_UART9_GATE_VAL    RK3588_UART_PCLK_GATE_VAL(9)
#define RK3588_UART9_GATE2_REG   RK3588_CLKGATE_CON(14)
#define RK3588_UART9_GATE2_VAL   RK3588_UART_GATE_VAL(5)
#define RK3588_UART9_PINMUX_M1   { { RK3588_IOMUX_H(4, 1), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO4_B5(rx)/B4(tx) func10 */
#define RK3588_UART9_PINMUX_M2   { { RK3588_IOMUX_H(3, 3), 0x00FF00AAUL }, \
                                   { 0, 0 } } /* GPIO3_D4(rx)/D5(tx) func10;
                              on-board anchor: reg reads 0x000000AA */

#endif /* UART_SOC_RK3588_H */

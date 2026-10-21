/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3568 SoC-level UART hardware facts: MMIO bases, GIC interrupt ids,
 * CRU clock select/gate registers and the full m0/m1(/m2) GRF pinmux
 * variant set for UART1~UART9.
 *
 * SoC-constant: identical for every board built on RK3568. The board
 * wiring choices (which ports are enabled, which pinmux group each port
 * uses, RS485 echo suppression) live in uart_board_cfg.h.
 *
 * Every value is transcribed from the mainline Linux sources and
 * cross-verified (see 《Modbus-RTU_UART驱动实现方案_rk3568.md》V0.17 §6
 * + appendix B):
 *   - bases / interrupt ids: arch/arm64/boot/dts/rockchip/rk356x-base.dtsi
 *     (uart1~9 = GIC_SPI 117~125, INTID = SPI + 32 = 149~157);
 *   - CLKSEL_CON / CLKGATE_CON numbers and gate bits:
 *     drivers/clk/rockchip/clk-rk3568.c;
 *   - pinmux groups: arch/arm64/boot/dts/rockchip/rk3568-pinctrl.dtsi.
 */

#ifndef UART_SOC_RK3568_H
#define UART_SOC_RK3568_H

#include <stdint.h>

/* ===========================================================================
 * CRU / GRF register formulas (RK3568 TRM layout).
 * ========================================================================= */
#define RK3568_CRU_BASE                0xFDD20000UL
#define RK3568_GRF_BASE                0xFDC60000UL

/* CLKSEL_CON(n)  = CRU + 0x100 + 4n  (clock source select / divider) */
#define RK3568_CLKSEL_CON(n)           (RK3568_CRU_BASE + 0x100UL + 4UL * (n))
/* CLKGATE_CON(n) = CRU + 0x300 + 4n  (clock gates, SET_TO_DISABLE) */
#define RK3568_CLKGATE_CON(n)          (RK3568_CRU_BASE + 0x300UL + 4UL * (n))

/* GRF iomux: 0x20 per GPIO bank, 8 bytes per group (A=0,B=1,C=2,D=3),
 * L covers pins 0..3, H covers pins 4..7; one 4-bit function field per
 * pin. GPIO0 is NOT reachable here (PMUGRF domain, separate base). */
#define RK3568_GRF_IOMUX_L(bank, grp)  (RK3568_GRF_BASE + ((bank) - 1) * 0x20UL + (grp) * 8UL)
#define RK3568_GRF_IOMUX_H(bank, grp)  (RK3568_GRF_IOMUX_L(bank, grp) + 4UL)

/* HIWORD16 write convention ([31:16] mask, [15:0] data): only the masked
 * bits of the target register change - no read-modify-write races with
 * the Linux master core sharing the CRU/GRF. */
#define RK3568_HIWORD16(mask, data)    (((uint32_t)(mask) << 16) | (uint32_t)(data))

/* ===========================================================================
 * Shared per-port clock values.
 * ========================================================================= */
/* sclk_uartN parent mux field [13:12]; the parent table in clk-rk3568.c
 * is { clk_uartN_src, clk_uartN_frac, xin24m } -> index 2 = 24 MHz
 * crystal. Independent of the PLLs the Linux master may re-rate at any
 * time (AMP). Defensive: the boot chain already selects it (appendix
 * B.3). */
#define RK3568_UART_CLKSEL_VAL_XIN24M  RK3568_HIWORD16(0x3000, 0x2000)

/* Clock gates are SET_TO_DISABLE (write 0 = enable). Each UART occupies
 * a 4-gate slot in a shared CLKGATE_CON register: bit 4k   = PCLK_UARTn
 * (apb / register access), bit 4k+1 = src path gate, bit 4k+2 = frac
 * path gate, bit 4k+3 = SCLK_UARTn (final baud clock). Only PCLK and
 * SCLK are opened - the mux feeds sclk directly from xin24m, so the
 * src/frac path stays closed. */
#define RK3568_UART_GATE_VAL_SLOT(k)   RK3568_HIWORD16(0x9UL << (4UL * (k)), 0)

/* ===========================================================================
 * Per-port facts: UART1 ~ UART9.
 * UART0 lives in the PMUCRU/PMUGRF domain and is not supported by this
 * driver (§4.2); RK3568_UART_PORT_MAX bounds the logical port id space.
 * ========================================================================= */
#define RK3568_UART_PORT_MIN           1U
#define RK3568_UART_PORT_MAX           9U

/* ---- UART1: GIC SPI 117, mux CLKSEL_CON(52), gates CON(27).12+15 ---- */
#define RK3568_UART1_BASE        0xFE650000UL
#define RK3568_UART1_INT_ID      149
#define RK3568_UART1_CLKSEL_REG  RK3568_CLKSEL_CON(52)
#define RK3568_UART1_GATE_REG    RK3568_CLKGATE_CON(27)
#define RK3568_UART1_GATE_VAL    RK3568_HIWORD16(0x9000, 0) /* not slot-aligned */
#define RK3568_UART1_PINMUX_M0   { { RK3568_GRF_IOMUX_L(2, 1), 0xF0002000UL }, \
                                   { RK3568_GRF_IOMUX_H(2, 1), 0x000F0002UL } } /* GPIO2_B3(rx)/B4(tx) func2 */
#define RK3568_UART1_PINMUX_M1   { { RK3568_GRF_IOMUX_H(3, 3), 0xFF004400UL }, \
                                   { 0, 0 } } /* GPIO3_D7(rx)/D6(tx) func4 */

/* ---- UART2: GIC SPI 118, mux CLKSEL_CON(54), gates CON(28) slot0 ---- */
#define RK3568_UART2_BASE        0xFE660000UL
#define RK3568_UART2_INT_ID      150
#define RK3568_UART2_CLKSEL_REG  RK3568_CLKSEL_CON(54)
#define RK3568_UART2_GATE_REG    RK3568_CLKGATE_CON(28)
#define RK3568_UART2_GATE_VAL    RK3568_UART_GATE_VAL_SLOT(0)
#define RK3568_UART2_PINMUX_M0_BOOTCHAIN \
    { { 0, 0 }, { 0, 0 } } /* GPIO0_D0(rx)/D1(tx) func1: PMUGRF domain,
                              already muxed by the boot chain - no GRF write */
#define RK3568_UART2_PINMUX_M1   { { RK3568_GRF_IOMUX_H(1, 3), 0x0FF00220UL }, \
                                   { 0, 0 } } /* GPIO1_D6(rx)/D5(tx) func2 */

/* ---- UART3: GIC SPI 119, mux CLKSEL_CON(56), gates CON(28) slot1 ---- */
#define RK3568_UART3_BASE        0xFE670000UL
#define RK3568_UART3_INT_ID      151
#define RK3568_UART3_CLKSEL_REG  RK3568_CLKSEL_CON(56)
#define RK3568_UART3_GATE_REG    RK3568_CLKGATE_CON(28)
#define RK3568_UART3_GATE_VAL    RK3568_UART_GATE_VAL_SLOT(1)
#define RK3568_UART3_PINMUX_M0   { { RK3568_GRF_IOMUX_L(1, 0), 0x00FF0022UL }, \
                                   { 0, 0 } } /* GPIO1_A0(rx)/A1(tx) func2 */
#define RK3568_UART3_PINMUX_M1   { { RK3568_GRF_IOMUX_L(3, 2), 0x000F0004UL }, \
                                   { RK3568_GRF_IOMUX_H(3, 1), 0xF0004000UL } } /* GPIO3_C0(rx)/B7(tx) func4 */

/* ---- UART4: GIC SPI 120, mux CLKSEL_CON(58), gates CON(28) slot2 ---- */
#define RK3568_UART4_BASE        0xFE680000UL
#define RK3568_UART4_INT_ID      152
#define RK3568_UART4_CLKSEL_REG  RK3568_CLKSEL_CON(58)
#define RK3568_UART4_GATE_REG    RK3568_CLKGATE_CON(28)
#define RK3568_UART4_GATE_VAL    RK3568_UART_GATE_VAL_SLOT(2)
#define RK3568_UART4_PINMUX_M0   { { RK3568_GRF_IOMUX_H(1, 0), 0x0FF00202UL }, \
                                   { 0, 0 } } /* GPIO1_A4(rx)/A6(tx) func2 */
#define RK3568_UART4_PINMUX_M1   { { RK3568_GRF_IOMUX_L(3, 1), 0x0FF00440UL }, \
                                   { 0, 0 } } /* GPIO3_B1(rx)/B2(tx) func4 */

/* ---- UART5: GIC SPI 121, mux CLKSEL_CON(60), gates CON(28) slot3 ---- */
#define RK3568_UART5_BASE        0xFE690000UL
#define RK3568_UART5_INT_ID      153
#define RK3568_UART5_CLKSEL_REG  RK3568_CLKSEL_CON(60)
#define RK3568_UART5_GATE_REG    RK3568_CLKGATE_CON(28)
#define RK3568_UART5_GATE_VAL    RK3568_UART_GATE_VAL_SLOT(3)
#define RK3568_UART5_PINMUX_M0   { { RK3568_GRF_IOMUX_L(2, 0), 0x0FF00330UL }, \
                                   { 0, 0 } } /* GPIO2_A1(rx)/A2(tx) func3 */
#define RK3568_UART5_PINMUX_M1   { { RK3568_GRF_IOMUX_L(3, 2), 0xFF004400UL }, \
                                   { 0, 0 } } /* GPIO3_C3(rx)/C2(tx) func4 */

/* ---- UART6: GIC SPI 122, mux CLKSEL_CON(62), gates CON(29) slot0 ---- */
#define RK3568_UART6_BASE        0xFE6A0000UL
#define RK3568_UART6_INT_ID      154
#define RK3568_UART6_CLKSEL_REG  RK3568_CLKSEL_CON(62)
#define RK3568_UART6_GATE_REG    RK3568_CLKGATE_CON(29)
#define RK3568_UART6_GATE_VAL    RK3568_UART_GATE_VAL_SLOT(0)
#define RK3568_UART6_PINMUX_M0   { { RK3568_GRF_IOMUX_L(2, 0), 0xF0003000UL }, \
                                   { RK3568_GRF_IOMUX_H(2, 0), 0x000F0003UL } } /* GPIO2_A3(rx)/A4(tx) func3 */
#define RK3568_UART6_PINMUX_M1   { { RK3568_GRF_IOMUX_H(1, 3), 0x0FF00330UL }, \
                                   { 0, 0 } } /* GPIO1_D6(rx)/D5(tx) func3 */

/* ---- UART7: GIC SPI 123, mux CLKSEL_CON(64), gates CON(29) slot1 ---- */
#define RK3568_UART7_BASE        0xFE6B0000UL
#define RK3568_UART7_INT_ID      155
#define RK3568_UART7_CLKSEL_REG  RK3568_CLKSEL_CON(64)
#define RK3568_UART7_GATE_REG    RK3568_CLKGATE_CON(29)
#define RK3568_UART7_GATE_VAL    RK3568_UART_GATE_VAL_SLOT(1)
#define RK3568_UART7_PINMUX_M0   { { RK3568_GRF_IOMUX_H(2, 0), 0x0FF00330UL }, \
                                   { 0, 0 } } /* GPIO2_A5(rx)/A6(tx) func3 */
#define RK3568_UART7_PINMUX_M1   { { RK3568_GRF_IOMUX_H(3, 2), 0x00FF0044UL }, \
                                   { 0, 0 } } /* GPIO3_C5(rx)/C4(tx) func4 */
#define RK3568_UART7_PINMUX_M2   { { RK3568_GRF_IOMUX_H(4, 0), 0xFF004400UL }, \
                                   { 0, 0 } } /* GPIO4_A3(rx)/A2(tx) func4 */

/* ---- UART8: GIC SPI 124, mux CLKSEL_CON(66), gates CON(29) slot2 ---- */
#define RK3568_UART8_BASE        0xFE6C0000UL
#define RK3568_UART8_INT_ID      156
#define RK3568_UART8_CLKSEL_REG  RK3568_CLKSEL_CON(66)
#define RK3568_UART8_GATE_REG    RK3568_CLKGATE_CON(29)
#define RK3568_UART8_GATE_VAL    RK3568_UART_GATE_VAL_SLOT(2)
#define RK3568_UART8_PINMUX_M0   { { RK3568_GRF_IOMUX_H(2, 2), 0x0FF00230UL }, \
                                   { 0, 0 } } /* GPIO2_C6(rx) func2 / C5(tx) func3 */
#define RK3568_UART8_PINMUX_M1   { { RK3568_GRF_IOMUX_L(3, 0), 0x000F0004UL }, \
                                   { RK3568_GRF_IOMUX_H(2, 3), 0xF0004000UL } } /* GPIO3_A0(rx)/GPIO2_D7(tx) func4 */

/* ---- UART9: GIC SPI 125, mux CLKSEL_CON(68), gates CON(29) slot3 ---- */
#define RK3568_UART9_BASE        0xFE6D0000UL
#define RK3568_UART9_INT_ID      157
#define RK3568_UART9_CLKSEL_REG  RK3568_CLKSEL_CON(68)
#define RK3568_UART9_GATE_REG    RK3568_CLKGATE_CON(29)
#define RK3568_UART9_GATE_VAL    RK3568_UART_GATE_VAL_SLOT(3)
#define RK3568_UART9_PINMUX_M0   { { RK3568_GRF_IOMUX_H(2, 0), 0xF0003000UL }, \
                                   { RK3568_GRF_IOMUX_L(2, 1), 0x000F0003UL } } /* GPIO2_A7(rx)/B0(tx) func3 */
#define RK3568_UART9_PINMUX_M1   { { RK3568_GRF_IOMUX_H(4, 2), 0x0FF00440UL }, \
                                   { 0, 0 } } /* GPIO4_C6(rx)/C5(tx) func4 */
#define RK3568_UART9_PINMUX_M2   { { RK3568_GRF_IOMUX_H(4, 0), 0x00FF0044UL }, \
                                   { 0, 0 } } /* GPIO4_A5(rx)/A4(tx) func4 */

#endif /* UART_SOC_RK3568_H */

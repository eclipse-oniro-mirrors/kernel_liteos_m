/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Rockchip RK3568 SoC definitions (AMP deployment, CPU3 runs liteos_m).
 *
 * Memory map references: RK3568 TRM + mainline rk3568.dtsi.
 *   GIC-600 : GICD 0xFD400000, GICR 0xFD460000 (RD stride 0x20000/core)
 *   UART1   : 0xFE650000 (unused)
 *   UART2   : 0xFE660000 (debug console, 16550-compatible, 32-bit access;
 *             the only UART wired to the board's debug USB port; it is
 *             SHARED with the Linux console (ttyFIQ0), so expect
 *             interleaved output)
 *   UART3   : 0xFE670000 (unused, not wired to the debug port)
 *
 * RTOS_DDR_BASE/RTOS_DDR_SIZE must stay in sync with the master-side
 * mcs reserved window (see board.ld and the master DTS oh_rt node):
 * image at 0x74100000, vrings master-allocated at 0x74000000..0x740FFFFF.
 */

#ifndef __SOC_H__
#define __SOC_H__

#include "los_typedef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

/* ---- RTOS DDR window (keep in sync with board.ld + master DTS) ---- */
#define RTOS_DDR_BASE                           0x74100000ULL
#define RTOS_DDR_SIZE                           0x03F00000ULL /* 63MB: 0x74100000..0x78000000 */

/* ---- GICv3 ---- */
#define GICD_BASE                               0xFD400000UL
#define GICR_BASE                               0xFD460000UL
/* one RD (0x10000) + one SGI (0x10000) frame per core */
#define GICR_STRIDE_CORES                       4

/* ---- UART ---- */
#define UART2_BASE                              0xFE660000UL /* debug console, shared with Linux */
#define UART_BASE                               UART2_BASE
#define UART_BAUDRATE                           1500000      /* board debug port line rate */
#define UART_INPUT_CLOCK                        24000000UL /* xin24m */

/* ---- MMU map (identity, see kernel/liteos_m/arch/arm64/los_mmu.c) ---- */
#define PERIPH_BASE_MMIO                        0xF0000000UL
#define PERIPH_SIZE_MMIO                        0x10000000UL /* 256MB: GIC/UART/CRU */
/* mcs reserved window: vrings (0x74000000..0x740FFFFF, Device-mapped in
 * los_mmu.c) + RTOS image (0x74100000.., Normal WB). The net DMA pools
 * sit in a Normal Non-Cacheable block at 0x76000000 (see gmac1_profile.h
 * NET_DMA_*). */
#define DDR_WIN_BASE_MMU                        0x74000000UL
#define DDR_WIN_SIZE_MMU                        0x04000000UL /* 64MB: 0x74000000..0x77FFFFFF */

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* __SOC_H__ */

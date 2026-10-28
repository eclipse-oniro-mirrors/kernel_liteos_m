/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * Rockchip RK3588 SoC definitions (AMP deployment, CPU7 runs liteos_m).
 *
 * Memory map references: RK3588 TRM + vendor rk3588.dtsi.
 *   GIC-600 : GICD 0xFE600000, GICR 0xFE680000 (1MB window, 8 RD/SGI
 *             frame pairs: cpu0-3 A55 cluster + cpu4-7 A76 cluster;
 *             per-core slot resolved by GICR_TYPER probe, see
 *             los_arch_interrupt.h GicrSlotProbe)
 *   UART2   : 0xFEB50000 (debug console, 16550-compatible, 32-bit access;
 *             SHARED with the Linux console (ttyFIQ0), expect interleaved
 *             output)
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
#define GICD_BASE                               0xFE600000UL
#define GICR_BASE                               0xFE680000UL
/* one RD (0x10000) + one SGI (0x10000) frame per core; 8 PEs */
#define GICR_STRIDE_CORES                       8
#define GICD_MMU_SIZE                           0x010000UL

/* ---- UART ---- */
#define UART2_BASE                              0xFEB50000UL /* debug console, shared with Linux */
#define UART_BASE                               UART2_BASE
#define UART_BAUDRATE                           1500000      /* board debug port line rate */
#define UART_INPUT_CLOCK                        24000000UL /* xin24m */

/* ---- MMU map (identity, see kernel/liteos_m/arch/arm64/los_mmu.c) ---- */
#define PERIPH_BASE_MMIO                        0xF0000000UL
#define PERIPH_SIZE_MMIO                        0x0F000000UL /* 240MB: 0xF0000000..0xFEFFFFFF; */
/* must not end at 0x1_0000_0000: dynamic-MMU MmuCheckAddrParam (VA_BITS=32)
 * rejects a region whose end equals the VA space limit */
/* mcs reserved window: vrings (0x74000000..0x740FFFFF, Device-mapped in
 * los_mmu.c) + RTOS image (0x74100000.., Normal WB). */
#define DDR_WIN_BASE_MMU                        0x74000000UL
#define DDR_WIN_SIZE_MMU                        0x04000000UL /* 64MB: 0x74000000..0x77FFFFFF */

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* __SOC_H__ */

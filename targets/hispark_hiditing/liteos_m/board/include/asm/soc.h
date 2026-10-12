/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 SoC register definitions
 */

#ifndef _SOC_H
#define _SOC_H

#include "soc_common.h"

#ifdef __ASSEMBLER__

/* Assembly-safe numeric constants (no C type suffixes or pointer dereference) */
#define UART0_BASE              0x5200B000
#define UART0_DATA_OFF          0x04
#define UART0_FIFO_STATUS_OFF    0x44
#define UART_TX_FIFO_FULL_BIT    0
#define WDT_BASE                0x57020000
#define WDT_KEY                 0x5A5A5A5A
#define WDT_CR_OFF               0x10

/* PMP CSR numbers */
#define PMPADDR20_CSR            0x3C4
#define PMPADDR21_CSR            0x3C5
#define PMPCFG5_CSR              0x3A5

/* PMP config values: each byte is one entry (entry0 in MSB).
 * 0x9b = R/X (entry20=itcm), 0x9d = R/W (entry21=dtcm) */
#define PMP_CFG_RW_RW            0x9b9b0000  /* itcm rw, dtcm rw */
#define PMP_CFG_RX_RW            0x9b9d0000  /* itcm rx, dtcm rw */

#else /* !__ASSEMBLER__ */

/* HI3322 system / bus clock */
#define OS_SYS_CLOCK             32000000UL

/* HI3322 timer (DW timer, TIMER0 used as OS tick) */
#define TIMER_BASE_ADDR          0x52003000u   /* timer sysctl / lock base */
#define TIMER0_BASE              (TIMER_BASE_ADDR + 0x100)  /* 0x52003100 */
#define TIMER0_LOAD_COUNT0      (TIMER0_BASE + 0x00)
#define TIMER0_LOAD_COUNT1      (TIMER0_BASE + 0x04)
#define TIMER0_CURRENT_COUNT    (TIMER0_BASE + 0x08)
#define TIMER0_CONTROL          (TIMER0_BASE + 0x10)
#define TIMER0_EOI              (TIMER0_BASE + 0x14)
#define TIMER_LOCK_BASE         TIMER_BASE_ADDR
#define TIMER_UNLOCK_VALUE       0x5A5A5A5Au
#define TIMER_CLOCK             32000000UL

/* HI3322 console UART. Physically this is UART2 on the chip (hs-fbb names
 * it UART2_BASE / SW_DEBUG_UART_BUS=UART_BUS_2 in
 * src/drivers/chips/3322/include/acore/platform_core.h); morpheus calls it
 * UART0 in software for brevity — same 0x5200B000 controller, DesignWare.
 * Register layout per hs-fbb src/drivers/drivers/driver/audio/source/core/hal/uart/serial_dw.c:
 *   0x04: data register (RBR/THR)
 *   0x44: FIFO status register (bit 0 = TX FIFO full) */
#define UART0_BASE              0x5200B000u
#define UART0_DATA              (*(volatile UINT32 *)(UART0_BASE + 0x04))
#define UART0_FIFO_STATUS       (*(volatile UINT32 *)(UART0_BASE + 0x44))
#define UART_TX_FIFO_FULL       (1u << 0)   /* bit 0: TX FIFO full — wait while set */

/* HI3322 Watchdog (WDT V151, chip WDT) */
#define WDT_BASE                0x57020000u
#define WDT_REG_LOCK        (*(volatile uint32_t *)(WDT_BASE + 0x00))
#define WDT_REG_LOAD        (*(volatile uint32_t *)(WDT_BASE + 0x04))
#define WDT_REG_RESTART     (*(volatile uint32_t *)(WDT_BASE + 0x08))
#define WDT_REG_EOI         (*(volatile uint32_t *)(WDT_BASE + 0x0C))
#define WDT_REG_CR          (*(volatile uint32_t *)(WDT_BASE + 0x10))
#define WDT_KEY             0x5A5A5A5Au
#define WDT_CLOCK_HZ        32768u
#define WDT_LOAD_RESEV      8

/* HI3322 CLIC (Core-Local Interrupt Controller, LinxCore).
 * Hardcoded base instead of read_custom_csr(0xFBF) — re-reading the CSR
 * each call returned inconsistent values on this chip (writes went to one
 * address, reads from another). Register layout: see
 * kernel/liteos_m/drivers/interrupt/include/clic.h */
#define CLIC_BASE_ADDR         0xb0000000u

#endif /* __ASSEMBLER__ */

/* Interrupt numbers — per hs-fbb chip_core_irq.h (LOCAL_INTERRUPT0 = 16):
 *   TIMER_0_IRQN = 16 + 27 = 43 (DW timer) */
#define NUM_HAL_INTERRUPT_TIMER 43

#define OS_TICK_INT_NUM         NUM_HAL_INTERRUPT_TIMER

/* RISC-V system interrupt IDs (standard, for liteos_m).
 * Hi3322 expands the standard 12 mie bits (0..11) with 14 custom mie bits
 * (12..25, e.g. NUM_HAL_INTERRUPT_CSIP=12), giving 26 system vectors.
 * Must match hs-fbb 3322 interrupt_config.h::OS_RISCV_SYS_VECTOR_CNT. */
#define RISCV_SYS_MAX_IRQ       25
#define RISCV_MACH_EXT_IRQ       11
#define RISCV_MACH_TIMER_IRQ    7

/* External interrupts: software IRQ 26..105 map 1:1 to CLIC register slots
 * 26..105 (CLIC_INTIP_OFF(i) = 0x1000 + 4*i). 80 external sources per
 * hs-fbb 3322 chip_core_irq.h (LOCAL_INTERRUPT0=16, enumerated to
 * MJPGE_INTR_IRQN = 16+89 = 105). Total OS_HWI_MAX_NUM = 26+80 = 106. */
#define RISCV_PLIC_VECTOR_CNT   80

#endif /* _SOC_H */

/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
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
 */

#ifndef _SOC_H
#define _SOC_H

#include "soc_common.h"

/* WS63 system clock */
#define OS_SYS_CLOCK             80000000UL

/* WS63 timer (TIMER0) */
#define TIMER0_BASE             0x44002100
#define TIMER0_LOAD_COUNT0      (TIMER0_BASE + 0x00)
#define TIMER0_LOAD_COUNT1      (TIMER0_BASE + 0x04)
#define TIMER0_CURRENT_COUNT    (TIMER0_BASE + 0x08)
#define TIMER0_CONTROL          (TIMER0_BASE + 0x10)
#define TIMER0_EOI              (TIMER0_BASE + 0x14)
#define TIMER_CLOCK             80000000UL

/* WS63 UART0 */
#define UART0_BASE              0x44010000
#define UART0_DATA              (*(volatile UINT32 *)(UART0_BASE + 0x04))
#define UART0_LINE_STATUS       (*(volatile UINT32 *)(UART0_BASE + 0x34))
#define UART_LS_THR_EMPTY       (1u << 6)  /* thre_s: TX holding register empty */
#define UART_LS_RX_READY        (1u << 5)  /* data_available: RX data ready */

/* WS63 Watchdog (WDT V151) */
#define WDT_BASE                0x40006000

/* Interrupt numbers */
#define NUM_HAL_INTERRUPT_TIMER 26
#define NUM_HAL_INTERRUPT_UART  53
#define NUM_HAL_INTERRUPT_NMI   12

#define OS_TICK_INT_NUM         NUM_HAL_INTERRUPT_TIMER

/* RISC-V system interrupt IDs (standard, for liteos_m) */
#define RISCV_SYS_MAX_IRQ       11
#define RISCV_MACH_EXT_IRQ       11
#define RISCV_MACH_TIMER_IRQ    7

/* PLIC: WS63 uses himideerv200 PLIC via CSR (libinterrupt.a).
 * No memory-mapped PLIC registers — PLIC operations go through
 * HalIrqInit/HalIrqUnmask/HalIrqMask/HalIrqSetPrio/HalIrqClear/HalCurIrqGet
 * which are CSR-based (cipri=0x7ED, prithd=0xBFE). */

#define RISCV_PLIC_VECTOR_CNT   96

#ifdef LOSCFG_SHELL
void ShellInputPollTaskInit(void);
#endif

#endif /* _SOC_H */

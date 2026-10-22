/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3588 (AArch64, AMP) target configuration.
 */

#include "soc.h"
#include "stdbool.h"

#ifndef _TARGET_CONFIG_H
#define _TARGET_CONFIG_H

/*=============================================================================
                                    System clock module configuration
=============================================================================*/
/* CNTFRQ programmed by TF-A (RK3568: 24MHz). The tick layer re-reads it at
 * runtime (los_timer.c); this constant is only the fallback/metadata value. */
#define SYSCLK_FREQ 24000000
#define OS_SYS_CLOCK SYSCLK_FREQ
#define LOSCFG_BASE_CORE_TICK_HW_TIME 0

#define LOSCFG_BASE_CORE_TICK_WTIMER 1
/* generic timer TVAL/CVAL are 32-bit signed counters */
#define LOSCFG_BASE_CORE_TICK_RESPONSE_MAX 0x7FFFFFFFUL

/* arch/arm64/los_timer.c: select the EL1 VIRTUAL timer backend (CNTV,
 * PPI 27) + polling tick pump. The RK3588 secure chain hands the slave to
 * NS EL1 with CNTHCTL_EL2.EL1PCEN/EL1PCTEN clear (every CNTP access traps
 * to a dead EL2) and leaves the CPU7 PPIs in Group0 (interrupts swallowed
 * at EL3). Boards with a normal boot chain (RK3568) keep the default
 * physical-timer + WFI backend by not defining this. */
#define LOSCFG_ARCH_TIMER_VIRT 1

/*=============================================================================
                                Hardware interrupt module configuration
=============================================================================*/
#define LOSCFG_PLATFORM_HWI 1
#define LOSCFG_USE_SYSTEM_DEFINED_INTERRUPT 1
/* GICv3 (GIC-600) on RK3588: single 64KB GICD frame at 0xFE600000, full
 * INTID range. Claimed-from-Linux SPIs live high: GMAC1 = 266,
 * CAN1 = 374, CAN2 = 375 - the limit must cover them (288, copied from
 * the RK3568 target, rejected CAN with ArchHwiCreate(374) error). */
#define LOSCFG_PLATFORM_HWI_LIMIT 1020

/*=============================================================================
                                Task module configuration
=============================================================================*/
/* AArch64: SP must stay 16-byte aligned (SPAlign check). */
#define LOSCFG_STACK_POINT_ALIGN_SIZE 16
#define LOSCFG_BASE_CORE_EXC_TSK_SWITCH 1

/*=============================================================================
                                Software timer module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_SWTMR 1
#define LOSCFG_BASE_CORE_SWTMR_ALIGN 0
#define LOSCFG_BASE_CORE_SWTMR_LIMIT 48

/*=============================================================================
                                Memory module configuration
=============================================================================*/
extern unsigned long __heap_start;
extern unsigned long __heap_size;
#define LOSCFG_SYS_EXTERNAL_HEAP 1
#define LOSCFG_SYS_HEAP_ADDR (void *)&__heap_start
#define LOSCFG_SYS_HEAP_SIZE (unsigned long)&__heap_size
#define OS_SYS_MEM_NUM 20
#define LOSCFG_MEMORY_BESTFIT 1

/* ==============================================================================
                                printf module configuration
============================================================================== */
#define LOSCFG_KERNEL_PRINTF 1

#define LOS_KERNEL_TEST_NOT_SMOKE 0

/*=============================================================================
                                Shell module configuration
=============================================================================*/
#define LOSCFG_USE_SHELL 1
#define LOSCFG_SHELL_PRIO 3

/*=============================================================================
                                Initcall mechanism
=============================================================================*/
#define LOSCFG_KERNEL_INITCALL 0

#endif /* _TARGET_CONFIG_H */

/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 target config (mirror WS63 target_config.h with #ifndef guards)
 *
 * All LOSCFG_* macros are guarded: kconfig-generated config.h takes precedence.
 * Board-specific (non-kconfig) values are unguarded.
 */

#ifndef _TARGET_CONFIG_H
#define _TARGET_CONFIG_H

#include "stdint.h"
#include "stdbool.h"
#include "soc.h"
#include "los_compiler.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/*=============================================================================
                    System clock module configuration
=============================================================================*/
#ifndef OS_SYS_CLOCK
#define OS_SYS_CLOCK                        32000000UL
#endif
#ifndef LOSCFG_BASE_CORE_TICK_PER_SECOND
#define LOSCFG_BASE_CORE_TICK_PER_SECOND    (100UL)
#endif
#ifndef LOSCFG_BASE_CORE_TICK_HW_TIME
#define LOSCFG_BASE_CORE_TICK_HW_TIME        0
#endif
#ifndef LOSCFG_BASE_CORE_TICK_WTIMER
#define LOSCFG_BASE_CORE_TICK_WTIMER        0
#endif
#ifndef LOSCFG_BASE_CORE_TICK_RESPONSE_MAX
#define LOSCFG_BASE_CORE_TICK_RESPONSE_MAX  ((UINT64)-1)
#endif

/*=============================================================================
                    Hardware interrupt configuration
=============================================================================*/
/* Hi3322 CLIC supports RISCV_PLIC_VECTOR_CNT (96) external interrupts
   plus the RISC-V system interrupts (RISCV_SYS_MAX_IRQ+1 = 12), giving
   OS_HWI_MAX_NUM = 108 in total (see los_arch_interrupt.h). The platform
   HWI limit must be >= NUM_HAL_INTERRUPT_TIMER (43, see soc.h), otherwise
   LOS_HwiCreate(TICK_IRQN) fails with LOS_ERRNO_HWI_NUM_INVALID. */
#ifndef LOSCFG_PLATFORM_HWI_LIMIT
#define LOSCFG_PLATFORM_HWI_LIMIT                          106
#endif

/*=============================================================================
                    Task module configuration
=============================================================================*/
#ifndef LOSCFG_BASE_CORE_TSK_LIMIT
#define LOSCFG_BASE_CORE_TSK_LIMIT                          28
#endif
/* With IRQ stack switch, task stacks only need scheduling chain (~400B).
   2KB is plenty (hs-fbb uses 3072 for 3322). */
#undef LOSCFG_BASE_CORE_TSK_IDLE_STACK_SIZE
#define LOSCFG_BASE_CORE_TSK_IDLE_STACK_SIZE                (0x800U)
#ifndef LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE
#define LOSCFG_BASE_CORE_TSK_DEFAULT_STACK_SIZE             (0x800U)
#endif
#ifndef LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE
#define LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE                 (0x400U)
#endif
#ifndef LOSCFG_BASE_CORE_TIMESLICE
#define LOSCFG_BASE_CORE_TIMESLICE                          1
#endif
#ifndef LOSCFG_BASE_CORE_TSK_MONITOR
#define LOSCFG_BASE_CORE_TSK_MONITOR                         1
#endif

/*=============================================================================
                    Software timer module configuration
=============================================================================*/
#ifndef LOSCFG_BASE_CORE_SWTMR
#define LOSCFG_BASE_CORE_SWTMR                              1
#endif
#ifndef LOSCFG_BASE_CORE_SWTMR_ALIGN
#define LOSCFG_BASE_CORE_SWTMR_ALIGN                        1
#endif
/* XIP flash executes timer callbacks slower than RAM; raise the
 * threshold so periodic callbacks don't flood UART with warnings
 * and starve the test task (swtmr is priority 0). */
/*=============================================================================
                    Memory module configuration
=============================================================================*/
extern UINTPTR g_intheap_begin;
extern UINTPTR g_intheap_size;
#ifndef LOSCFG_SYS_EXTERNAL_HEAP
#define LOSCFG_SYS_EXTERNAL_HEAP                            1
#endif
#ifndef LOSCFG_SYS_HEAP_ADDR
#define LOSCFG_SYS_HEAP_ADDR                                (VOID *)&g_intheap_begin
#endif
#ifndef LOSCFG_SYS_HEAP_SIZE
#define LOSCFG_SYS_HEAP_SIZE                                (UINTPTR)&g_intheap_size
#endif
#ifndef LOSCFG_BASE_MEM_NODE_SIZE_CHECK
#define LOSCFG_BASE_MEM_NODE_SIZE_CHECK                     0
#endif
#ifndef LOSCFG_MEM_MUL_POOL
#define LOSCFG_MEM_MUL_POOL                                 1
#endif
#ifndef OS_SYS_MEM_NUM
#define OS_SYS_MEM_NUM                                      20
#endif
#ifndef LOSCFG_KERNEL_MEM_SLAB
#define LOSCFG_KERNEL_MEM_SLAB                              0
#endif
#ifndef LOSCFG_MEMORY_BESTFIT
#define LOSCFG_MEMORY_BESTFIT                               1
#endif
/*=============================================================================
                    Exception module configuration
=============================================================================*/
#ifndef LOSCFG_PLATFORM_HWI_WITH_ARG
#define LOSCFG_PLATFORM_HWI_WITH_ARG                        1
#endif
#ifndef LOSCFG_KERNEL_PRINTF
#define LOSCFG_KERNEL_PRINTF                                3
#endif

/*=============================================================================
                    Shell module configuration
=============================================================================*/
#ifndef LOSCFG_USE_SHELL
#define LOSCFG_USE_SHELL                                    0
#endif

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* _TARGET_CONFIG_H */

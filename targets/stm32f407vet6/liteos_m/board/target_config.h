/**@defgroup los_config System configuration items
 * @ingroup kernel
 */

#include "soc.h"
#include "stdbool.h"

#ifndef _TARGET_CONFIG_H
#define _TARGET_CONFIG_H

/*=============================================================================
                                    System clock module configuration
=============================================================================*/
/* HSI 16MHz reset default, no PLL setup in this demo */
#define OS_SYS_CLOCK                                        SYSCLK_FREQ
#define LOSCFG_BASE_CORE_TICK_HW_TIME                       0
#define LOSCFG_BASE_CORE_TICK_WTIMER                        0
#define LOSCFG_BASE_CORE_TICK_RESPONSE_MAX                  0xFFFFFFUL

/*=============================================================================
                                Hardware interrupt module configuration
=============================================================================*/
#define LOSCFG_PLATFORM_HWI                                 1
#define LOSCFG_USE_SYSTEM_DEFINED_INTERRUPT                 1
#define LOSCFG_PLATFORM_HWI_LIMIT                           128
/*=============================================================================
                                       Task module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_EXC_TSK_SWITCH                     1

/* IPC module configuration (SEM/MUX/QUEUE/EVENT) managed by Kconfig + vendor defconfig */

/*=============================================================================
                                       Software timer module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_SWTMR                              1
#define LOSCFG_BASE_CORE_SWTMR_ALIGN                        1
#define LOSCFG_BASE_CORE_SWTMR_IN_ISR                       1
/*=============================================================================
                                       Memory module configuration
=============================================================================*/
extern unsigned int __heap_start;
extern unsigned int __heap_size;
#define LOSCFG_SYS_EXTERNAL_HEAP                            1
#define LOSCFG_SYS_HEAP_ADDR                                (void *)&__heap_start
#define LOSCFG_SYS_HEAP_SIZE                                (unsigned int)&__heap_size
#define OS_SYS_MEM_NUM                                      20
#define LOSCFG_MEMORY_BESTFIT                               1

/* =============================================================================
                                       printf module configuration
============================================================================= */
#define LOSCFG_KERNEL_PRINTF                                1

#define LOS_KERNEL_TEST_NOT_SMOKE                           0

/*=============================================================================
                                        Initcall mechanism
=============================================================================*/
#define LOSCFG_KERNEL_INITCALL                              1

#endif /* _TARGET_CONFIG_H */

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
#define OS_SYS_CLOCK                        80000000UL
#define LOSCFG_BASE_CORE_TICK_HW_TIME       0
#define LOSCFG_BASE_CORE_TICK_WTIMER        0
#define LOSCFG_BASE_CORE_TICK_RESPONSE_MAX  ((UINT64)-1)

/*=============================================================================
                    Task module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_TIMESLICE                          1
#define LOSCFG_BASE_CORE_TSK_MONITOR                        1

/*=============================================================================
                    IPC module configuration (SEM/MUX/QUEUE/EVENT) managed by Kconfig + vendor defconfig
=============================================================================*/

/*=============================================================================
                    Software timer module configuration
=============================================================================*/
#define LOSCFG_BASE_CORE_SWTMR                              1
#define LOSCFG_BASE_CORE_SWTMR_ALIGN                        1
#define LOSCFG_BASE_CORE_SWTMR_LIMIT                        16
/*=============================================================================
                    Memory module configuration
=============================================================================*/
extern UINTPTR g_intheap_begin;
extern UINTPTR g_intheap_size;
#define LOSCFG_SYS_EXTERNAL_HEAP                            1
#define LOSCFG_SYS_HEAP_ADDR                                (VOID *)&g_intheap_begin
#define LOSCFG_SYS_HEAP_SIZE                                (UINTPTR)&g_intheap_size
#define LOSCFG_MEM_MUL_POOL                                 1
#define OS_SYS_MEM_NUM                                      20
#define LOSCFG_KERNEL_MEM_SLAB                              0
#define LOSCFG_MEMORY_BESTFIT                               1
/*=============================================================================
                    Exception module configuration
=============================================================================*/
#define LOSCFG_KERNEL_PRINTF                                1

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

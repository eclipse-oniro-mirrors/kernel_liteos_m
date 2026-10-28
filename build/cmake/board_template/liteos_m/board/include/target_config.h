/*
 * target_config.h —— board 模板。
 * 复制并填入你的板子值。这是生效的 C 层配置，经 los_config.h 被内核消费
 * （不是 menuconfig.h——后者仅为参考快照；见 docs/integration-guide.md）。
 *
 * 约定（来自 3322 参考）：所有 LOSCFG_* 宏都用 #ifndef 守护，
 * 这样 kconfig 生成的 config.h 优先；板级专属（非 kconfig）值不加守护。
 * 分叉守护检查的值（默认 TICK/TIMESLICE/HWI_PRIO/CPUP）必须在此定义。
 */

#ifndef _TARGET_CONFIG_H
#define _TARGET_CONFIG_H

#include "stdint.h"
#include "stdbool.h"
#include "soc.h"                /* 你的 SoC 头 */
#include "los_compiler.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/*=============================================================================
                    系统时钟与 tick
=============================================================================*/
#ifndef OS_SYS_CLOCK
#define OS_SYS_CLOCK                        32000000UL   /* 你的时钟 */
#endif

/* === 分叉守护的值（必须覆盖 GN 基线）=== */
#ifndef LOSCFG_BASE_CORE_TICK_PER_SECOND
#define LOSCFG_BASE_CORE_TICK_PER_SECOND    (1000UL)
#endif
#ifndef LOSCFG_BASE_CORE_TIMESLICE_TIMEOUT
#define LOSCFG_BASE_CORE_TIMESLICE_TIMEOUT  (2000UL)
#endif
#ifndef LOSCFG_HWI_PRIO_LIMIT
#define LOSCFG_HWI_PRIO_LIMIT               (7)
#endif
#ifndef LOSCFG_CPUP_SAMPLE_PERIOD
#define LOSCFG_CPUP_SAMPLE_PERIOD            (10000)
#endif

/*=============================================================================
                    内存——内核堆
=============================================================================*/
#ifndef LOSCFG_SYS_HEAP_ADDR
#define LOSCFG_SYS_HEAP_ADDR                (VOID *)&g_intheap_begin  /* 你的链接符号 */
#endif

/*=============================================================================
                    任务 / IPC 限额（按你的 RAM 调整）
=============================================================================*/
#ifndef LOSCFG_BASE_CORE_TSK_LIMIT
#define LOSCFG_BASE_CORE_TSK_LIMIT          (28)
#endif
#ifndef LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE
#define LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE (1024)
#endif
#ifndef LOSCFG_BASE_CORE_TSK_IDLE_STACK_SIZE
#define LOSCFG_BASE_CORE_TSK_IDLE_STACK_SIZE (2048)
#endif
#ifndef LOSCFG_BASE_CORE_SWTMR_LIMIT
#define LOSCFG_BASE_CORE_SWTMR_LIMIT        (16)
#endif

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* _TARGET_CONFIG_H */

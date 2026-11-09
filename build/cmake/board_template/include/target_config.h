/*
 * target_config.h —— board 模板。
 * 复制并填入你的板子值。这是生效的 C 层配置，经 los_config.h 被内核消费
 * （不是 menuconfig.h——后者仅为参考快照；见 docs/integration-guide.md）。
 *
 * 约定（来自 3322 参考）：所有 LOSCFG_* 宏都用 #ifndef 守护，
 * 这样 kconfig 生成的 config.h 优先；板级专属（非 kconfig）值不加守护。
 * 分叉守护检查的值（默认 TICK/TIMESLICE/HWI_PRIO/CPUP/QUEUE_LIMIT）必须在此
 * 定义（CMakeLists.txt 的 EVOLVE_DIVERGE_GUARD，缺失即 FATAL_ERROR）。
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
#ifndef LOSCFG_BASE_IPC_QUEUE_LIMIT
#define LOSCFG_BASE_IPC_QUEUE_LIMIT         (64)   /* GN 基线 30；3322 参考 64 */
#endif

/*=============================================================================
                    架构硬性要求（riscv）
============================================================================*/
/* arch/risc-v/include/arch/los_arch_interrupt.h:43 对 0 值（含未定义，#if
 * 语义按 0）直接 #error —— 所有 riscv 板必须显式定义为 1。 */
#ifndef LOSCFG_PLATFORM_HWI_WITH_ARG
#define LOSCFG_PLATFORM_HWI_WITH_ARG        1
#endif

/*=============================================================================
                    tick 模式
============================================================================*/
/* 二选一：TICK_PERIODIC（周期 tick，3322 参考）或 tickless（需
 * LOSCFG_KERNEL_TICKLESS_GLOBAL + SortLinkAttribute::baseTime，见
 * los_sched_tick.c:79 的 Tick Mode Abstraction）。不定义两者则掉进
 * tickless 分支且缺 baseTime 成员——编译失败。 */
#ifndef LOSCFG_KERNEL_TICK_PERIODIC
#define LOSCFG_KERNEL_TICK_PERIODIC          1
#endif
/* tick 计数器上限——los_sched_tick.c:48 对 0 值（含未定义）直接 #error。 */
#ifndef LOSCFG_BASE_CORE_TICK_RESPONSE_MAX
#define LOSCFG_BASE_CORE_TICK_RESPONSE_MAX  ((UINT64)-1)
#endif

/*=============================================================================
                    内存——内核堆
=============================================================================*/
/* 分配器选择——los_memory_pri.h 按此切换 LosMemPoolInfo 布局；不定义任何
 * 选择宏时类型解析失败。3322 参考用 BESTFIT（与 kernel_configs/debug.config
 * 的 LOSCFG_KERNEL_MEM_BESTFIT=y 对应）。 */
#ifndef LOSCFG_KERNEL_MEM_BESTFIT
#define LOSCFG_KERNEL_MEM_BESTFIT             1
#endif
/* membox（静态定长盒分配）——快照无条件编入 kernel/base/mem/membox/los_membox.c，
 * 其 LOS_MEMBOX_NODE/结构成员要求这两个宏（3322 同）。 */
#ifndef LOSCFG_KERNEL_MEMBOX
#define LOSCFG_KERNEL_MEMBOX                  1
#endif
#ifndef LOSCFG_KERNEL_MEMBOX_STATIC
#define LOSCFG_KERNEL_MEMBOX_STATIC           1
#endif
/* 多内存池——快照把 kernel/base/mem/common/multipool/los_multipool.c 编入
 * （无模块门禁），其 nextPool 成员要求 LOSCFG_MEM_MUL_POOL==1（3322 同）。 */
#ifndef LOSCFG_MEM_MUL_POOL
#define LOSCFG_MEM_MUL_POOL                    1
#endif
#ifndef LOSCFG_SYS_HEAP_ADDR
#define LOSCFG_SYS_HEAP_ADDR                (VOID *)&g_intheap_begin  /* 你的链接符号 */
#endif
/* 内核堆链接符号——由你的链接脚本提供（3322 参考： target_config.h:331）*/
extern void *g_intheap_begin;

/*=============================================================================
                    任务 / IPC 限额（按你的 RAM 调整）
=============================================================================*/
/* 任务栈分配模式——los_task.c 对两者皆缺直接 #error，至少二选一。
 * 3322 参考用 DYNAMIC。 */
#ifndef LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION
#define LOSCFG_TASK_STACK_DYNAMIC_ALLOCATION  1
#endif
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

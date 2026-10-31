#ifndef _ASM_INTERRUPT_CONFIG_H
#define _ASM_INTERRUPT_CONFIG_H

/* Board contract header for the generic timer drivers (drivers/timer).
 * SysTick is vector slot 15 (SysTick_IRQn + OS_SYS_VECTOR_CNT). */

/* Explicit override: los_config.h may have set a TRACE fallback (0xff). */
#undef OS_TICK_INT_NUM
#define OS_TICK_INT_NUM         15

#endif /* _ASM_INTERRUPT_CONFIG_H */

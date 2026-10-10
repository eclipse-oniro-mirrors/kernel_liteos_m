/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 RISC-V HAL header (mirror WS63 riscv_hal.h)
 */

#ifndef _RISCV_HAL_H
#define _RISCV_HAL_H

#include "los_compiler.h"
#include "los_timer.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

extern CHAR *__except_stack_top;
extern CHAR *__start_and_irq_stack_top;
extern CHAR *__bss_end;

extern VOID HalIrqDisable(UINT32 vector);
extern VOID HalIrqEnable(UINT32 vector);

extern VOID HalPlicInit(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* _RISCV_HAL_H */

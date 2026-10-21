/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 board-level initialization interface.
 */

#ifndef _BOARD_H
#define _BOARD_H

#include "los_compiler.h"
#include "asm/memmap_config.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

extern char __sys_mem_start;
extern char __sys_mem_end;

extern VOID hi3322_board_init(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
#endif /* _BOARD_H */

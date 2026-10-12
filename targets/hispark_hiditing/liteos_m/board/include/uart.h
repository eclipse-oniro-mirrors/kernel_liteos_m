/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 board-level UART abstraction header.
 */

#ifndef _UART_H
#define _UART_H

#include "los_compiler.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

extern VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock);
extern INT32 UartGetc(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
#endif /* _UART_H */

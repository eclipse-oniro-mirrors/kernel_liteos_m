/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 * RK3568 UART3 (16550-compatible) porting interface.
 */

#ifndef __UART_H__
#define __UART_H__

#include "los_typedef.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

extern VOID UartInit(VOID);
extern INT32 UartPutc(INT32 c, VOID *file);
extern VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock);
extern INT32 UartGetc(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* __UART_H__ */

#ifndef _UART_H
#define _UART_H

#include "los_compiler.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

extern VOID UartInit(VOID);
extern INT32 UartPutc(INT32 c, VOID *file);
extern VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock);
extern INT32 UartGetc(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */
#endif

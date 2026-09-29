#ifndef __UART_H__
#define __UART_H__
#include "los_typedef.h"
extern VOID UartInit(VOID);
extern INT32 UartPutc(INT32 c, VOID *file);
extern VOID UartPuts(const CHAR *s, UINT32 len, BOOL isLock);
extern INT32 UartGetc(VOID);
#endif

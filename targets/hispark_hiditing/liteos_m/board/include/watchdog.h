/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 watchdog interface declarations.
 */

#ifndef _WATCHDOG_H
#define _WATCHDOG_H

#include "los_compiler.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

extern VOID hi3322_watchdog_init(UINT32 timeoutSeconds, UINT8 mode);
extern VOID hi3322_watchdog_feed(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
#endif /* _WATCHDOG_H */

/*
 * Copyright (c) 2026-2026. All rights reserved.
 *
 * Cache maintenance interface test suite (ws63 RISC-V arch/cache.h).
 * Covers: enable/disable-size validation, prefetch validation, full
 * flush/invalidate, by-VA flush/clean/invalidate, by-addr wrappers,
 * alignment-boundary behaviour and zero-size edge cases.
 */

#ifndef _IT_LOS_CACHE_H
#define _IT_LOS_CACHE_H

#include "osTest.h"
#include "arch/cache.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cplusplus */
#endif /* __cplusplus */

extern VOID ItLosCache001(VOID);
extern VOID ItLosCache002(VOID);
extern VOID ItLosCache003(VOID);
extern VOID ItLosCache004(VOID);
extern VOID ItLosCache005(VOID);
extern VOID ItLosCache006(VOID);
extern VOID ItLosCache007(VOID);
extern VOID ItLosCache008(VOID);
extern VOID ItLosCache009(VOID);
extern VOID ItLosCache010(VOID);
extern VOID ItLosCache011(VOID);
extern VOID ItLosCache013(VOID);
extern VOID ItLosCache014(VOID);
extern VOID ItLosCache015(VOID);
extern VOID ItLosCache016(VOID);
extern VOID ItLosCache017(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cplusplus */
#endif /* __cplusplus */

#endif /* _IT_LOS_CACHE_H */

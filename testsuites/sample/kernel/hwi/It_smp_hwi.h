#ifndef LOS_SMP_HWI_H
#define LOS_SMP_HWI_H
#include "los_hwi.h"
#include "osTest.h"
/*
 * liteos_new has no shared-IRQ support: HwiCreate on a taken IRQ returns
 * LOS_ERRNO_HWI_ALREADY_CREATED (0x2000904), never SHARED_ERROR. liteos_a
 * platforms without sharing define LOSCFG_NO_SHARED_IRQ to switch the ported
 * cases to the ALREADY_CREATED expectation branch.
 */
#define LOSCFG_NO_SHARED_IRQ 1
#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif /* __cpluscplus */
#endif /* __cpluscplus */
#ifdef LOSCFG_KERNEL_SMP
VOID ItSmpLosHwi001(VOID);
VOID ItSmpLosHwi002(VOID);
VOID ItSmpLosHwi003(VOID);
VOID ItSmpLosHwi004(VOID);
VOID ItSmpLosHwi005(VOID);
VOID ItSmpLosHwi006(VOID);
VOID ItSmpLosHwi007(VOID);
VOID ItSmpLosHwi008(VOID);
VOID ItSmpLosHwi009(VOID);
VOID ItSmpLosHwi010(VOID);
VOID ItSmpLosHwi011(VOID);
VOID ItSmpLosHwi012(VOID);
VOID ItSmpLosHwi013(VOID);
VOID ItSmpLosHwiIpi001(VOID);
VOID ItSmpLosHwiIpi002(VOID);
VOID ItSmpLosHwiIpi003(VOID);
VOID ItSmpLosHwiIpi004(VOID);
VOID ItSmpLosHwiIpi005(VOID);
VOID ItSmpLosHwiIpi006(VOID);
VOID ItSmpLosHwiIpi007(VOID);
VOID ItSmpLosHwiIpi008(VOID);
#endif
#ifdef __cplusplus
#if __cplusplus
}
#endif /* __cpluscplus */
#endif /* __cpluscplus */

extern VOID ItSmpLosHwi001(VOID);
extern VOID ItSmpLosHwi002(VOID);
extern VOID ItSmpLosHwi003(VOID);
extern VOID ItSmpLosHwi004(VOID);
extern VOID ItSmpLosHwi005(VOID);
extern VOID ItSmpLosHwi006(VOID);
extern VOID ItSmpLosHwi007(VOID);
extern VOID ItSmpLosHwi008(VOID);
extern VOID ItSmpLosHwi009(VOID);
extern VOID ItSmpLosHwi010(VOID);
extern VOID ItSmpLosHwi011(VOID);
extern VOID ItSmpLosHwi012(VOID);
extern VOID ItSmpLosHwi013(VOID);
extern VOID ItSmpLosHwiIpi001(VOID);
extern VOID ItSmpLosHwiIpi002(VOID);
extern VOID ItSmpLosHwiIpi003(VOID);
extern VOID ItSmpLosHwiIpi004(VOID);
extern VOID ItSmpLosHwiIpi005(VOID);
extern VOID ItSmpLosHwiIpi006(VOID);
extern VOID ItSmpLosHwiIpi007(VOID);
extern VOID ItSmpLosHwiIpi008(VOID);
#endif /* LOS_SMP_HWI_H */

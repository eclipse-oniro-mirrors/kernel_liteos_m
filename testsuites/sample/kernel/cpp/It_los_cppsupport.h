/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: cppsupport test module declarations
 */

#ifndef _IT_LOS_CPPSUPPORT_H
#define _IT_LOS_CPPSUPPORT_H

#include "osTest.h"
#include "los_cppsupport.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

VOID ItLosCppsupport001(VOID);
VOID ItLosCppsupport002(VOID);
VOID ItLosCppsupport003(VOID);
VOID ItLosCppsupport004(VOID);
VOID ItLosCppsupport005(VOID);

UINT32 CppTestCtorCount(VOID);
UINT32 CppTestMagic(VOID);
UINT32 CppTestCxaAtexitCall(VOID);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif /* _IT_LOS_CPPSUPPORT_H */

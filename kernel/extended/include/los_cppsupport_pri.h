/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: cppsupport private declarations
 */

#ifndef _LOS_CPPSUPPORT_PRI_H
#define _LOS_CPPSUPPORT_PRI_H

#include "los_compiler.h"
#include "los_cppsupport.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern CHAR __fast_end;

#ifdef LOSCFG_AARCH64
extern UINT8 __eh_frame_begin;
VOID __register_frame(VOID *begin);
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_CPPSUPPORT_PRI_H */

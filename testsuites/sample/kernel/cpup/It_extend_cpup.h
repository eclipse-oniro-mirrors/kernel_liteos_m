/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above notice, this list of
 * conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor that of its contributors may be used
 * to endorse or promote products derived from this software without specific prior
 * written permission.
 */

#ifndef IT_EXTEND_CPUP_H
#define IT_EXTEND_CPUP_H

#include "los_cpup.h"
#include "los_task.h"
#include "osTest.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

#define CPU_USE_MODE1 1
#define CPU_USE_MODE0 0
#define CPU_USE_MIN 0
#define TASK_STATUS_UNDETACHED 0

#define CPUP_TEST_TOLERANT 50
#define CPUP_BACKWARD 2

#ifndef LOS_CPUP_SINGLE_CORE_PRECISION
/* task usage is normalized against a single core's capacity, so a task
 * occupying one core fully reports LOS_CPUP_PRECISION (permillage), the
 * same way liteos_a reports LOS_CPUP_SINGLE_CORE_PRECISION. */
#define LOS_CPUP_SINGLE_CORE_PRECISION LOS_CPUP_PRECISION
#endif

#ifdef LOSCFG_CPUP_INCLUDE_IRQ
extern UINT32 GetNopCount(void);
extern UINT32 TestGetSingleHwiCpup(UINT32 hwi, UINT32 mode);
#endif

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

#endif

/*
 * Copyright (c) 2026 Huawei Device Co., Ltd. All rights reserved.
 * Description: C++ support public API
 */

/**
 * @defgroup los_cppsupport C++
 * @ingroup kernel
 */

#ifndef _LOS_CPPSUPPORT_H
#define _LOS_CPPSUPPORT_H

#include "los_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @ingroup los_cppsupport
 * If LOS_CppSystemInit() is called in the first stage of scatter load,
 * this flag should be passed as the third parameter.
 */
#define BEFORE_SCATTER 0

/**
 * @ingroup los_cppsupport
 * If LOS_CppSystemInit() is called in the second stage of scatter load,
 * this flag should be passed as the third parameter.
 */
#define AFTER_SCATTER 1

/**
 * @ingroup los_cppsupport
 * If scatter load is disabled, this flag should be passed as the third
 * parameter when LOS_CppSystemInit() is called.
 */
#define NO_SCATTER 2

/**
 * @ingroup los_cppsupport
 * @brief System cppsupport initialization.
 *
 * @par Description:
 * This API is used to initialize the cppsupport.
 * @attention
 * <ul>
 * <li>initArrayStart is the start address of .init_array section,
 * initArrayEnd is the end address of .init_array section.</li>
 * <li>initArrayStart must be smaller than initArrayEnd, initArrayStart
 * and initArrayEnd should be 4(32 bits platform) or 8(64 bits platform) bytes alignment.</li>
 * </ul>
 *
 * @param  initArrayStart  [IN] Start address of init_array section.
 * @param  initArrayEnd    [IN] End address of init_array section.
 * @param  flag            [IN] The flag indicating when LOS_CppSystemInit() is called,
 *                              #BEFORE_SCATTER, #AFTER_SCATTER or #NO_SCATTER.
 *
 * @retval #LOS_OK         Initialization succeeded.
 * @retval #LOS_NOK        The entered parameter is incorrect, initialization failed.
 * @par Dependency:
 * <ul><li>los_cppsupport.h: the header file that contains the API declaration.</li></ul>
 */
extern INT32 LOS_CppSystemInit(UINTPTR initArrayStart, UINTPTR initArrayEnd, INT32 flag);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _LOS_CPPSUPPORT_H */

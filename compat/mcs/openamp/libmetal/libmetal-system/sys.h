/*
 * Copyright (c) 2017, Linaro Limited. and Contributors. All rights reserved.
 * Copyright (c) 2026, Huawei Technologies Co., Ltd. All rights reserved.
 * Adapted for LiteOS-M (RK3568 AMP slave core).
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*
 * @file	liteos/sys.h
 * @brief	LiteOS-M system primitives for libmetal.
 */

#ifndef __METAL_SYS__H__
#error "Include metal/sys.h instead of metal/liteos/sys.h"
#endif

#ifndef __METAL_LITEOS_SYS__H__
#define __METAL_LITEOS_SYS__H__

#ifdef __cplusplus
extern "C" {
#endif

#define METAL_INIT_DEFAULTS				\
{							\
	.log_handler	= metal_default_log_handler,	\
	.log_level	= METAL_LOG_INFO,		\
}

/** Structure of libmetal runtime state. */
struct metal_state {
	/** Common (system independent) data. */
	struct metal_common_state common;
};

#ifdef __cplusplus
}
#endif

#endif /* __METAL_LITEOS_SYS__H__ */

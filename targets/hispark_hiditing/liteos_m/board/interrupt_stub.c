/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: Board-level provider for the CLIC mtvt vector table.
 *              The liteos_m hwi framework uses g_hwiControllerOps (a STATIC
 *              variable inside los_interrupt.c) accessed via HwiControllerOpsGet(),
 *              so no board-level g_hwiOps definition is needed here.
 *              OsIntHandle, g_intCount and IntActive are provided by the
 *              kernel (los_hwi.c).
 */

#include "los_compiler.h"
#include "los_interrupt.h"
#include "los_hwi_pri.h"
#include "los_arch_interrupt.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/* CLIC mtvt vector table — match hs-fbb's g_vectorIntTable. We use non-
 * vector mode (clicintattr.bit0=0) so the hardware dispatches via mtvec
 * and never indexes this table, but reset_vector.S still points mtvt
 * at it (mirroring hs-fbb) to avoid leaving mtvt at seliteos's leftover
 * value. Aligned 64 per CLIC shv-mode requirement. */
__attribute__((aligned(64))) HWI_PROC_FUNC g_vectorIntTable[OS_HWI_MAX_NUM];

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

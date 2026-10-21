/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 RISC-V HAL glue. CLIC init is in
 *              kernel/liteos_m/drivers/interrupt/riscv_lcmp_clic.c
 *              (function HalIrqInit, called by HalPlicInit below; the arch
 *              HalHwiInit in arch/risc-v/src/los_interrupt.c only points
 *              mtvec at HalTrapVector). This file provides
 *              the board-level HAL wrappers (HalIrqEnable/Disable,
 *              HalBackTraceFpCheck, HalPlicInit) that
 *              main.c and the system tick timer call.
 */

#include "riscv_hal.h"
#include "los_debug.h"
#include "soc.h"
#include "los_arch_interrupt.h"
#include "clic.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

VOID HalIrqDisable(UINT32 vector)
{
    if (vector <= RISCV_SYS_MAX_IRQ) {
        CLEAR_CSR(mie, 1 << vector);
    } else {
        HwiControllerOps *ops = HwiControllerOpsGet();
        if ((ops != NULL) && (ops->disableIrq != NULL)) {
            ops->disableIrq(vector);
        }
    }
}

VOID HalIrqEnable(UINT32 vector)
{
    if (vector <= RISCV_SYS_MAX_IRQ) {
        SET_CSR(mie, 1 << vector);
    } else {
        HwiControllerOps *ops = HwiControllerOpsGet();
        if ((ops != NULL) && (ops->enableIrq != NULL)) {
            ops->enableIrq(vector);
        }
    }
}

BOOL HalBackTraceFpCheck(UINT32 value)
{
    if (value >= (UINT32)(UINTPTR)(&__bss_end)) {
        return TRUE;
    }

    if ((value >= (UINT32)(UINTPTR)(&__start_and_irq_stack_top)) && (value < (UINT32)(UINTPTR)(&__except_stack_top))) {
        return TRUE;
    }

    return FALSE;
}

VOID HalPlicInit(VOID)
{
    HalIrqInit();
    HalIrqEnable(RISCV_MACH_EXT_IRQ);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

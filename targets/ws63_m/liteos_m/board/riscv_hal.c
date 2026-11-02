/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * RISC-V HAL for WS63 — wraps the himideerv200 PLIC source driver
 * (kernel/liteos_m/drivers/interrupt/riscv_himideerv200_plic.c).
 *
 * All PLIC hardware operations (enable/disable/priority/clear/get) are
 * CSR-based (himideerv200 custom CSRs: LOCIPRI=0xBC0+, LOCIEN=0xBE0+,
 * LOCIPCLR=0xBF0, PRITHD=0xBFE) — no SiFive-style memory-mapped
 * registers. Formerly this wrapped the prebuilt libinterrupt.a.
 */

#include "riscv_hal.h"
#include "los_debug.h"
#include "soc.h"
#include "los_arch_interrupt.h"
#include "los_hwi.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/* libinterrupt.a PLIC operations (CSR-based, himideerv200) */
extern VOID HalIrqInit(VOID);
extern UINT32 HalIrqUnmask(UINT32 hwiNum);
extern UINT32 HalIrqMask(UINT32 hwiNum);
extern UINT32 HalIrqClear(UINT32 hwiNum);
extern UINT32 HalIrqSetPrio(UINT32 hwiNum, UINT16 priority);
extern UINT32 HalCurIrqGet(VOID);

VOID HalIrqDisable(UINT32 vector)
{
    if (vector <= RISCV_SYS_MAX_IRQ) {
        CLEAR_CSR(mie, 1 << vector);
    } else {
        HalIrqMask(vector);
    }
}

VOID HalIrqEnable(UINT32 vector)
{
    if (vector <= RISCV_SYS_MAX_IRQ) {
        SET_CSR(mie, 1 << vector);
    } else {
        HalIrqUnmask(vector);
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

VOID HalPlicIrqDispatch(VOID *arg)
{
    (VOID)arg;
    UINT32 irq = HalCurIrqGet();   /* claim pending IRQ from PLIC; 0 if none */
    if (irq == 0 || irq >= OS_HWI_MAX_NUM) {
        return;
    }
    HalHwiInterruptDone(irq);      /* dispatch g_hwiHandleForm[irq] → handler */
    HalIrqClear(irq);              /* complete PLIC (EOI) */
}

VOID HalPlicInit(VOID)
{
    HalIrqInit();

    HalIrqEnable(RISCV_MACH_EXT_IRQ);

    /* Register the PLIC external-IRQ dispatcher at the machine external
     * interrupt slot (RISCV_MACH_EXT_IRQ = cause 11). The trap entry
     * (los_exc.S HalTrapVector) reads mcause, masks to the cause code, and
     * calls HalHwiInterruptDone(cause) — for external IRQs that is 11, NOT
     * the real PLIC IRQ number (e.g. TIMER_1_IRQN). This handler claims the
     * real IRQ from the PLIC, dispatches it, and completes the PLIC. */
    (VOID)LOS_HwiCreate(RISCV_MACH_EXT_IRQ, 0x1, 0, HalPlicIrqDispatch, NULL);
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif

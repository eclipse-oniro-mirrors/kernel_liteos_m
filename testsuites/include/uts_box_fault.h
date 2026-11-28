/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 * of conditions and the following disclaimer in the documentation and/or other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * UTS BOX fault isolation matrix - LOSCFG_TEST_BOX_FAULT builds only.
 *
 * One box raises a CPU fault on purpose from its own user-mode code, after
 * its regular suite. The kernel must destroy exactly that box and let the
 * other box and the kernel keep running:
 *
 *   failing box   suite, then UtsRaiseBoxFault(): it must never return
 *   survivor      its whole suite, starting once the kernel confirmed the
 *                 destruction (or, mid-case variant, already under way)
 *   kernel        osTest.c UtsCheckIsolation(): verify the recorded fault,
 *                 release the survivor, then re-exercise the kernel services
 *                 (uts_post_fault.c)
 *
 * Every other outcome counts as a failure: the fault instruction returning,
 * the survivor being destroyed or damaged, the wrong fault type or address,
 * a timeout, a post-fault kernel check failing.
 *
 * UtsRaiseBoxFault(): foreignEntry  code of the other box (kind 3)
 *                     victimAddr    private RAM of the other box (kind 1, 2)
 *                     armStateEntry even address of executable code (kind 5)
 *
 * Kind 8 is the reverse case: the fault is raised inside a syscall, in kernel
 * context. The kernel must NOT contain it - the system halts with
 * "BOX_EXC: ... NOT contained, reason=3" and nothing is printed afterwards.
 */
#ifndef _UTS_BOX_FAULT_H
#define _UTS_BOX_FAULT_H

#include "los_typedef.h"
#include "uts_phase.h"

#ifdef LOSCFG_TEST_BOX_FAULT
#if (LOSCFG_TEST_BOX_FAULT_KIND == 8)
#include "los_task.h"
/* The failing box's entry task: box id + 1 in the task id's upper half. */
#define UTS_FAULT_TASK_ID ((UTS_FAULT_BOX + 1U) << 16)
#endif

/* Box that raises the fault, and the one that must keep running. */
#define UTS_FAULT_BOX        LOSCFG_TEST_BOX_FAULT_BOX
#define UTS_SURVIVOR_BOX     (1U - LOSCFG_TEST_BOX_FAULT_BOX)

/* Diagnostic type (los_arch_interrupt.h) the kernel must have recorded. */
#if (LOSCFG_TEST_BOX_FAULT_KIND <= 2)
#define UTS_FAULT_TYPE       OS_EXC_MF_DACCVIOL
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 3)
#define UTS_FAULT_TYPE       OS_EXC_MF_IACCVIOL
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 5)
#define UTS_FAULT_TYPE       OS_EXC_UF_INVSTATE
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 7)
#define UTS_FAULT_TYPE       OS_EXC_UF_UNALIGNED
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 6)
/* Escalated to HardFault: the entry asm reports the cause in bits 15:8 over
 * the HardFault cause in bits 7:0 (los_exc.S, OS_EXC_CAUSE_HARDFAULT). */
#define UTS_FAULT_TYPE       (((UINT32)OS_EXC_UF_UNDEFINSTR << 8) | (UINT32)OS_EXC_CAUSE_HARDFAULT)
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 8)
#define UTS_FAULT_TYPE       OS_EXC_UF_UNALIGNED
#else
#define UTS_FAULT_TYPE       OS_EXC_UF_UNDEFINSTR
#endif

/*
 * Mid-case variant: the surviving boxA starts at GO and is already inside its
 * own suite when boxB raises the fault. boxB waits until boxA has passed
 * UTS_FAULT_MIDCASE_CASES cases and then preempts it (the kernel coordinator
 * raises boxB's entry priority for this variant, after the kernel phase).
 * Only meaningful when boxB fails.
 */
#if defined(LOSCFG_TEST_BOX_FAULT_SURVIVOR_EARLY) && (LOSCFG_TEST_BOX_FAULT_BOX == 1)
#define UTS_FAULT_MIDCASE        1
#define UTS_FAULT_MIDCASE_CASES  20U
#else
#define UTS_FAULT_MIDCASE        0
#endif

extern UINT32 ICunitGetWorldPass(UINT32 world);
extern UINT32 ICunitGetWorldFail(UINT32 world);
extern VOID boxAEntry(VOID);
extern VOID boxBEntry(VOID);
extern VOID UtsPostFaultKernelCheck(VOID);

#if defined(__GNUC__)
/*
 * Raise the configured fault from the failing box's user-mode code. The probe
 * is marked ARMED first so the kernel can tell "faulted as asked" from "never
 * reached the probe"; the caller parks if this returns, and RETURNED turns
 * the run into a failure.
 */
static inline VOID UtsRaiseBoxFault(UINTPTR foreignEntry, UINTPTR victimAddr, UINTPTR armStateEntry)
{
    (VOID)foreignEntry;
    (VOID)victimAddr;
    (VOID)armStateEntry;
    g_utsProbeState = UTS_PROBE_ARMED;
#if (LOSCFG_TEST_BOX_FAULT_KIND == 1)
    /* Load from the other box's private heap: MemManage DACCVIOL. The
     * scratch word is only written if the access unexpectedly succeeds. */
    g_utsProbeScratch = *(volatile const UINT32 *)victimAddr;
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 2)
    /* Store to the other box's private heap: MemManage DACCVIOL. */
    *(volatile UINT32 *)victimAddr = 0x5A5A5A5AU;
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 3)
    /* Call the other box's code: the instruction fetch lands outside the
     * running box's code region, MemManage IACCVIOL at the call target. */
    {
        VOID (*volatile entry)(VOID) = (VOID (*)(VOID))foreignEntry;
        entry();
    }
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 5)
    /* Branch to an even address, in code the box may execute: ARM state
     * does not exist on Cortex-M, UsageFault INVSTATE. (The target must be
     * executable by the box: on a forbidden address the instruction access
     * violation is taken first, measured with 0x08000000.) */
    __asm volatile ("bx %0" : : "r"(armStateEntry) : "memory");
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 7)
    /* Word load one byte off alignment. CCR.UNALIGN_TRP is set on this build
     * family (drivers/interrupt/arm_nvic.c), so it is a UsageFault UNALIGNED.
     * Hand-written because the test TUs are built -mno-unaligned-access. */
    {
        UINT32 buf[4] = {0U, 0U, 0U, 0U};
        UINT32 value = 0U;
        __asm volatile ("ldr %0, [%1]" : "=r"(value) : "r"((UINTPTR)buf + 1U) : "memory");
        g_utsProbeScratch = value;
    }
#elif (LOSCFG_TEST_BOX_FAULT_KIND == 8)
    /* Kernel context: the syscall stores 32-bit fields through an odd pointer,
     * which traps with CCR.UNALIGN_TRP (set by drivers/interrupt/arm_nvic.c). */
    (VOID)LOS_TaskInfoGet(UTS_FAULT_TASK_ID, (TSK_INFO_S *)(UINTPTR)(victimAddr + 1U));
#else
    /* 0xDEFF is UDF #255: UsageFault UNDEFINSTR, or HardFault when the
     * UsageFault vector is disabled (kind 6). */
    __asm volatile (".hword 0xDEFF" ::: "memory");
#endif
    g_utsProbeState = UTS_PROBE_RETURNED;
}
#endif /* __GNUC__ */

#endif /* LOSCFG_TEST_BOX_FAULT */
#endif /* _UTS_BOX_FAULT_H */

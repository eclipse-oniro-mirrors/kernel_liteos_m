/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 *    conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
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

/* ============================================================================
 * It_los_boxA_usr_mux_util.h - shared plumbing for the usr-world BOX mux cases.
 *
 * One case per .c file, all in mux/. The helpers are declared here and DEFINED
 * ONCE in It_los_boxA_usr_mux_util.c, matching the mem/ case convention:
 *   - one definition instead of one copy per translation unit;
 *   - the spawn/wait helpers stay breakpointable;
 *   - unused helpers are dropped by --gc-sections.
 *
 * Every case runs in boxA USER mode through the shared iCunit runner invoked
 * by usr_demo/tboxA/boxA_test_main.c. The mux API must bind the PLAIN names:
 * LOS_MuxCreate is the COMMON_TEXT usr wrapper in los_usr_api.c and the other
 * entries are COMMON_TEXT veneers. The #ifdef self-check below catches a TU
 * accidentally compiled under the kernel rename root (which would bind the
 * kernel-flash SYS_ implementations and fault on user-mode entry).
 *
 * BoxA's mux pool is finite (LOSCFG_BOXA_MUX_LIMIT, currently 4) and the box
 * heap lock already owns one slot, so the cases never assume the kernel
 * LOSCFG_BASE_IPC_MUX_LIMIT. They create only what they need and delete it
 * before returning: a leaked mux would poison every later usr case.
 *
 * File names keep "boxA": boxes.ld routes objects into the boxA slot by file
 * name, and a GN object is "<target>.<source>.o" regardless of subdirectory,
 * which is why mux/ needs no linker-script change.
 * ========================================================================== */

#ifndef _IT_LOS_BOXA_MUX_UTIL_H
#define _IT_LOS_BOXA_MUX_UTIL_H

#include "osTest.h"
#include "syscalls.h"

/* Same Kconfig knob as the kernel mux suite. When it is off, BUILD.gn does not
 * compile these TUs at all; this guard keeps a hand-built object harmless and
 * makes the dependency explicit. */
#if (LOSCFG_TEST_KERNEL_MUX == 1)

#ifdef LOS_MuxCreate
#error "usr test TU compiled under the kernel rename root: check BUILD.gn configs"
#endif

/* The smallest stack a box task may have (LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE)
 * and the priority used by the helper children. The entry task runs at 0, so a
 * child at 5 never preempts a case body except while it explicitly yields. */
#define BOX_MUX_TASK_STACK_SIZE  0x400U
#define BOX_MUX_CHILD_PRIO       5U

/* Poll budget: this board ticks at ~1s, so a stuck child costs at most
 * BOX_MUX_WAIT_SPIN * BOX_MUX_WAIT_TICKS seconds before the case reports. */
#define BOX_MUX_WAIT_SPIN        20U
#define BOX_MUX_WAIT_TICKS       2U

/* Let a returned child run its exit trampoline, then hand its user stack back
 * to the box heap (the two-stage recycle documented by MEM-TSK-002). */
#define BOX_MUX_REAP_TICKS       5U

/* Bounded pend used by the timeout case. */
#define BOX_MUX_TIMEOUT_TICKS    5U

/* LOS_TaskStackRecycle is a usr-world wrapper, not a syscall entry, so it has
 * no prototype in syscalls.h. */
extern VOID LOS_TaskStackRecycle(VOID);

/* Create one box task whose user stack comes from the box heap (the usr
 * LOS_TaskCreate wrapper). Returns the kernel error code verbatim. */
extern UINT32 BoxMuxSpawn(TSK_ENTRY_FUNC entry, const CHAR *name, UINT32 *taskId);

/* Poll `state` until it equals `expect`, yielding between rounds. Returns TRUE
 * when the value was reached. */
extern BOOL BoxMuxWaitState(const volatile UINT32 *state, UINT32 expect);

/* One tick for the exit trampoline, then drain the queued user stacks. */
extern VOID BoxMuxReap(VOID);

#endif /* LOSCFG_TEST_KERNEL_MUX */

#endif /* _IT_LOS_BOXA_MUX_UTIL_H */

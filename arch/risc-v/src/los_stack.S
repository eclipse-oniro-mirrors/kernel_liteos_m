/*
 * Copyright (c) 2026-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Description: RISC-V generic system-stack allocation (irq/startup/exc/nmi).
 *              Excluded when LOSCFG_USING_BOARD_RESET_VECTOR is set (board
 *              defines its own symbols).
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list
 *    of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list
 *    of conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef LOSCFG_USING_BOARD_RESET_VECTOR

.global __irq_stack_top
.global __startup_stack_top
.global __exc_stack_top
.global __nmi_stack_top
/* Alias kept for code that historically uses __except_stack_top. */
.global __except_stack_top

.section ".int_stack", "wa", %nobits
.align 4  /* 2^4 byte alignment for stacks */

__irq_stack:
    .space LOSCFG_IRQ_STACK_SIZE
__irq_stack_top:

__startup_stack:
#ifdef LOSCFG_STARTUP_STACK_SIZE
#if LOSCFG_STARTUP_STACK_SIZE > 0
    .space LOSCFG_STARTUP_STACK_SIZE
#endif
#endif
__startup_stack_top:

#ifdef LOSCFG_EXC_STACK_SIZE
#if LOSCFG_EXC_STACK_SIZE > 0
__exc_stack:
    .space LOSCFG_EXC_STACK_SIZE
__exc_stack_top:
__except_stack_top = __exc_stack_top
#else
__exc_stack_top = __startup_stack_top
__except_stack_top = __startup_stack_top
#endif
#else
__exc_stack_top = __startup_stack_top
__except_stack_top = __startup_stack_top
#endif

#ifdef LOSCFG_NMI_STACK_SIZE
#if LOSCFG_NMI_STACK_SIZE > 0
__nmi_stack:
    .space LOSCFG_NMI_STACK_SIZE
__nmi_stack_top:
#else
__nmi_stack_top = __exc_stack_top
#endif
#else
__nmi_stack_top = __exc_stack_top
#endif

#endif /* !LOSCFG_USING_BOARD_RESET_VECTOR */

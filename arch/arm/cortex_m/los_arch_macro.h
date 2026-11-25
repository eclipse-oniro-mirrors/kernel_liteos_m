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

/*
 * Assembler pseudo-op abstraction layer, following the fbb unified cortex_m
 * pattern (self_src/arch_lite/arm/cortex_m/include/arch/macro.h): every
 * los_*.S in this directory is built by BOTH GNU as and IAR iasmarm from a
 * single source. The compiler is detected by the assembler's own predefined
 * macro — __IASMARM__ for iasmarm, __GNUC__ for GNU as. Bodies are written
 * once in unified-syntax lowercase mnemonics that both assemblers accept.
 *
 * Constraints this layer encodes:
 *  - iasmarm defines __IASMARM__ (NOT __ICCARM__, which is the C compiler's
 *    macro), so no build-system define plumbing is needed.
 *  - iasmarm needs no unwind pseudo-ops: FNSTART/CANTUNWIND/FNEND are empty.
 *  - GNU as needs .type annotations for STT_FUNC symbols; FUNC emits them.
 *
 * NOTE: iasmarm syntax below mirrors the legacy cortex-m33/iar port (MACRO
 * name arg ... ENDM, EXPORT/IMPORT/EQU); must be recompiled with iasmarm on
 * a real IAR toolchain before use.
 */

#ifndef _LOS_ARCH_MACRO_H
#define _LOS_ARCH_MACRO_H

#ifdef __IASMARM__

FUNC MACRO symbol
    EXPORT symbol
    ENDM

FUNCLOCAL MACRO symbol
    ENDM

EXTERNAL MACRO symbol
    IMPORT symbol
    ENDM

EQUAL MACRO name, value
name EQU value
    ENDM

FNSTART MACRO
    ENDM

CANTUNWIND MACRO
    ENDM

FNEND MACRO
    ENDM

#else /* __GNUC__ */

.MACRO FUNC symbol
    .global \symbol
    .type \symbol, %function
.ENDM

/* Like FUNC but keeps the label local (STT_FUNC, no .global): a global
 * branch target forces the wide 4-byte encoding. */
.MACRO FUNCLOCAL symbol
    .type \symbol, %function
.ENDM

.MACRO EXTERNAL symbol
    .extern \symbol
.ENDM

.MACRO EQUAL name, value
    .equ \name, \value
.ENDM

.MACRO FNSTART
    .fnstart
.ENDM

.MACRO CANTUNWIND
    .cantunwind
.ENDM

.MACRO FNEND
    .fnend
.ENDM

#endif /* __IASMARM__ */

#endif /* _LOS_ARCH_MACRO_H */

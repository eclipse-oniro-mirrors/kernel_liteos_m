/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
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

/* AArch64 hardware exception implementation (VBAR_EL1 vector table). */

    .extern OsExceptSyncExcHdl
    .extern OsCurrentELGet
    .extern _osExceptIrqHdl
    .extern _osExceptFiqHdl

#include "arch/regs.h"

#define ALIGN .align
#define ORG .org
#define MACRO   .macro
#define ENDM    .endm

    LR  .req    x30

    .arch armv8-a
    .text
    ALIGN 12

MACRO push_reg reg1, reg2
    STP   \reg1, \reg2, [SP,#-16]!
ENDM

MACRO pop_reg reg1, reg2
    LDP   \reg1, \reg2, [SP], #16
ENDM

MACRO push_all
    SUB  SP, SP, #32
    push_reg X28, X29
    push_reg X26, X27
    push_reg X24, X25
    push_reg X22, X23
    push_reg X20, X21
    push_reg X18, X19
    push_reg X16, X17
    push_reg X14, X15
    push_reg X12, X13
    push_reg X10, X11
    push_reg X8, X9
    push_reg X6, X7
    push_reg X4, X5
    push_reg X2, X3
    push_reg X0, X1
    MOV  X0, X0  //STANDBY
    MRS  X1, ELR_ELx
    MRS  X2, SPSR_ELx
    STP  LR, X0, [SP, #0xf0]
    STP  X1, X2, [SP, #0xf0 + 16]
ENDM

MACRO pop_all
    LDP  LR, X0, [SP, #0xf0]
    LDP  X1, X2, [SP, #0xf0 + 16]
    MSR  ELR_ELx, X1
    MSR  SPSR_ELx, X2
    pop_reg X0, X1
    pop_reg X2, X3
    pop_reg X4, X5
    pop_reg X6, X7
    pop_reg X8, X9
    pop_reg X10, X11
    pop_reg X12, X13
    pop_reg X14, X15
    pop_reg X16, X17
    pop_reg X18, X19
    pop_reg X20, X21
    pop_reg X22, X23
    pop_reg X24, X25
    pop_reg X26, X27
    pop_reg X28, X29
    ADD SP, SP, #32
ENDM

    .global arm64_exception_handlers
    .type   arm64_exception_handlers,function
arm64_exception_handlers:
    .type   arm64_exc_cur_el_SP0,function
arm64_exc_cur_el_SP0:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x080
    .type arm64_irq_cur_el_SP0,function
arm64_irq_cur_el_SP0:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x100
    .type arm64_fiq_cur_el_SP0,function
arm64_fiq_cur_el_SP0:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x180
    .type arm64_err_exc_cur_el_SP0,function
arm64_err_exc_cur_el_SP0:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x200
    .type arm64_sync_exc_cur_el_SPx,function
arm64_sync_exc_cur_el_SPx:
    push_all
    MSR   DAIFSET, #3
    MRS   X0, CurrentEL
    BL    OsCurrentELGet
    MOV   X0, SP
    BL    OsExceptSyncExcHdl
    B     arm64_exc_pop_all

    ORG 0x280
    .type arm64_irq_cur_el_SPx,function
arm64_irq_cur_el_SPx:
    B _osExceptIrqHdl

    ORG 0x300
    .type arm64_fiq_cur_el_SPx,function
arm64_fiq_cur_el_SPx:
#ifdef LOSCFG_ARCH_SECURE_MONITOR_MODE
    B _osExceptFiqHdl
#else
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .
#endif

    ORG 0x380
    .type arm64_err_exc_cur_el_SPx,function
arm64_err_exc_cur_el_SPx:
    push_all
    MSR   DAIFSET, #7
    MRS   X0, CurrentEL
    BL    OsCurrentELGet
    MOV   X0, SP
    BL    OsExceptSyncExcHdl
    B     arm64_exc_pop_all

    ORG 0x400
    .type arm64_sync_exc_lower_el_64,function
arm64_sync_exc_lower_el_64:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x480
    .type arm64_irq_lower_el_64,function
arm64_irq_lower_el_64:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x500
    .type arm64_fiq_lower_el_64,function
arm64_fiq_lower_el_64:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x580
    .type arm64_err_exc_lower_el_64,function
arm64_err_exc_lower_el_64:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x600
    .type arm64_sync_exc_lower_el_32,function
arm64_sync_exc_lower_el_32:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x680
    .type arm64_irq_lower_el_32,function
arm64_irq_lower_el_32:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x700
    .type arm64_fiq_lower_el_32,function
arm64_fiq_lower_el_32:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    ORG 0x780
    .type arm64_err_exc_lower_el_32,function
arm64_err_exc_lower_el_32:
    MRS X0, CurrentEL
    BL  OsCurrentELGet
    B   .

    .type arm64_exc_push_all,function
arm64_exc_push_all:
    push_all
    RET

    .type arm64_exc_pop_all,function
arm64_exc_pop_all:
    pop_all
    ERET

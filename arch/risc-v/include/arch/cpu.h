/* ----------------------------------------------------------------------------
 * Copyright (c) 2013-2020, Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Description: RISC-V CPU ops — layered stack-frame size constants + CSR
 *              aliases (TES/INT_AUTO/300S stripped; FPU via
 *              !LOSCFG_ARCH_FPU_DISABLE).
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
 * --------------------------------------------------------------------------- */

#ifndef _ARCH_CPU_H
#define _ARCH_CPU_H

#ifndef __ASSEMBLER__
#include "los_typedef.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Load/store mnemonics & slot widths. rv32 uses lw/sw; FPU is fld/fsd (D ext)
 * or flw/fsw (F ext). */
#ifdef __riscv64
#define LREG                    ld
#define SREG                    sd
#define FLREG                   fld
#define FSREG                   fsd
#define REGBYTES                8
#define FPUREGBYTES             8
#else
#define LREG                    lw
#define SREG                    sw
#define REGBYTES                4
#ifndef LOSCFG_ARCH_FPU_DISABLE
#ifdef LOSCFG_ARCH_FPU_DOUBLE
#define FLREG                   fld
#define FSREG                   fsd
#define FPUREGBYTES             8
#else
#define FLREG                   flw
#define FSREG                   fsw
#define FPUREGBYTES             4
#endif
#endif
#endif

/* Layered frame (low→high at sp): CPU_SPECIAL, FPU, INTEGER_CALLEE,
 * INTEGER_CALLER. EXC_SPECIAL (CXCPTSC/mcause/mtval/gp) is prepended only
 * in the exception frame. */
#define TP_STACK_INDEX              2
#define SP_STACK_INDEX              3

#define EXC_SPECIAL_REG_NUM         4   /* CXCPTSC, mcause, mtval, gp */
#define EXC_SPECIAL_REG_SIZE        (4 * REGBYTES)

#define INTEGER_CALLER_REG_NUM      16  /* t0-t6, a0-a7, ra */
#define INTEGER_CALLEE_REG_NUM      12  /* s0-s11 */
#define CPU_SPECIAL_REG_NUM         4   /* mstatus, mepc, tp, sp */
#define INTEGER_REG_NUM             (INTEGER_CALLER_REG_NUM + INTEGER_CALLEE_REG_NUM + CPU_SPECIAL_REG_NUM)

#define INTEGER_CALLER_REG_SIZE     (INTEGER_CALLER_REG_NUM * REGBYTES)
#define INTEGER_CALLEE_REG_SIZE     (INTEGER_CALLEE_REG_NUM * REGBYTES)
#define CPU_SPECIAL_REG_SIZE        (CPU_SPECIAL_REG_NUM * REGBYTES)
#define INTEGER_REG_SIZE            (INTEGER_CALLER_REG_SIZE + INTEGER_CALLEE_REG_SIZE + CPU_SPECIAL_REG_SIZE)

#ifndef LOSCFG_ARCH_FPU_DISABLE
#define FPU_STATUS_REG_NUM          4   /* fcsr, res[3] */
#define FPU_CALLER_REG_NUM          20  /* ft0-ft11, fa0-fa7 */
#define FPU_CALLEE_REG_NUM          12  /* fs0-fs11 */
#define FPU_STATUS_REG_SIZE         (FPU_STATUS_REG_NUM * REGBYTES)
#define FPU_CALLER_REG_SIZE         (FPU_CALLER_REG_NUM * FPUREGBYTES)
#define FPU_CALLEE_REG_SIZE         (FPU_CALLEE_REG_NUM * FPUREGBYTES)
#define FPU_EXTENSION_REG_SIZE      (FPU_CALLER_REG_SIZE + FPU_CALLEE_REG_SIZE)
#define FCSR_STACK_OFFSET           (CPU_SPECIAL_REG_SIZE + FPU_EXTENSION_REG_SIZE)
#define INT_SIZE_ON_STACK           (INTEGER_REG_SIZE + FPU_EXTENSION_REG_SIZE + FPU_STATUS_REG_SIZE)
#else
#define INT_SIZE_ON_STACK           INTEGER_REG_SIZE
#endif

/* Offset of the INTEGER_CALLEE region within the full frame. */
#ifndef LOSCFG_ARCH_FPU_DISABLE
#define INTEGER_CALLEE_POSITION     (CPU_SPECIAL_REG_SIZE + FPU_EXTENSION_REG_SIZE + FPU_STATUS_REG_SIZE)
#else
#define INTEGER_CALLEE_POSITION     (CPU_SPECIAL_REG_SIZE)
#endif

/* mstatus bits */
#define LOS_MSTATUS_MIE         0x00000008
#define LOS_MSTATUS_MPIE        0x00000080
#define LOS_MSTATUS_MPP         0x00001800
#define LOS_MSTATUS_FS          0x00006000
#ifdef LOSCFG_ARCH_LINX_M
#define LOS_MCAUSE_UOP          0x0100F000
#endif
#ifndef LOSCFG_ARCH_FPU_DISABLE
#define LOSCFG_MSTATUS_M        (LOS_MSTATUS_MPP | LOS_MSTATUS_MPIE | LOS_MSTATUS_FS)
#else
#define LOSCFG_MSTATUS_M        (LOS_MSTATUS_MPP | LOS_MSTATUS_MPIE)
#endif
#define UINT32_CUT_MASK         0xFFFFFFFF
#define UINT8_CUT_MASK          0xFF

#ifdef LOSCFG_ARCH_LINX_M
#define NUM_HAL_INTERRUPT_NMI   0xFFF
#endif

/* CSR aliases (uppercase → standard lowercase; TES remapping stripped). */
#define MSTATUS         mstatus
#define MEPC            mepc
#define MCAUSE          mcause
#define MTVEC           mtvec
#define MTVT            0x307
#define MINTTHRESH      mintthresh
#define MTVAL           mtval
#define MSCRATCH        mscratch
#define CXCPTSC         0xFC2
#define MSCRATCHCSWL    mscratchcswl
#define MINTSTATUS      mintstatus

#define TASK_CB_KERNEL_SP       0x0
#define TASK_CB_STATUS          0x4

/* mcause classification bits */
#define REG_HARD_FAULT_MASK     0x40000000
#define REG_LOCK_UP_MASK        0x80000000
#define MCAUSE_MASK_INT_BIT     0x80000000
#define MCAUSE_MPIL_BIT         16
#define MINTSTATUS_MIL_BIT      24
#define TMESEPS_PTES_MASK       0x100

#ifdef LOSCFG_ARCH_LINX_M
#define MCAUSE_MASK_INT_NUM     0x00000FFF
#else
#define MCAUSE_MASK_INT_NUM     0x7FFFFFFF
#endif

/* Legacy aliases for older code (linx131/trap.S etc.). */
#define MCAUSE_INT_ID_MASK      MCAUSE_MASK_INT_NUM
#define TRAP_INTERRUPT_MODE_MASK MCAUSE_MASK_INT_BIT
#define RISCV_MSTATUS_MIE       LOS_MSTATUS_MIE
#define RISCV_MSTATUS_MPIE      LOS_MSTATUS_MPIE
#define RISCV_MSTATUS_MPP       LOS_MSTATUS_MPP
#define RISCV_MSTATUS_FS        LOS_MSTATUS_FS

#ifdef __cplusplus
}
#endif

#endif /* _ARCH_CPU_H */

/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2026 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary, with or without modification,
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

#ifndef _KERNEL_VERIFY_H
#define _KERNEL_VERIFY_H

#include "kernel_verify_layout.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/*
 * kernel_verify: detects whether the running OS is liteos_m by directly
 * reading task control blocks, stack magic words and the stack pointer;
 * returns bitmask error codes.
 */

/*
 * Calling contract: task context only. KernelVerifyCurSp/KernelVerifyAll
 * read the current SP register and are meaningless in interrupt context.
 */

typedef enum {
    KERNEL_VERIFY_OK                     = 0,
    KERNEL_VERIFY_ERR_SP_NULL            = 0x01,

    KERNEL_VERIFY_ERR_SP_ALIGN           = 0x02,
    KERNEL_VERIFY_ERR_STATUS             = 0x04,
    KERNEL_VERIFY_ERR_PRIO               = 0x08,
    KERNEL_VERIFY_ERR_BOTTOM_MAGIC       = 0x10,
    KERNEL_VERIFY_ERR_STACK_RANGE        = 0x20,
    KERNEL_VERIFY_ERR_CUR_SP_RANGE       = 0x40,
    KERNEL_VERIFY_ERR_ARRAY_NULL         = 0x80,
    KERNEL_VERIFY_ERR_CUR_SP_NO_OWNER    = 0x100,
    KERNEL_VERIFY_ERR_CUR_SP_MULTI_OWNER = 0x200,
    KERNEL_VERIFY_ERR_RUNNING_COUNT      = 0x800,
    KERNEL_VERIFY_ERR_NOT_RUNNING        = 0x2000,
} kernel_verify_err_t;

/* Verify the first three TCB fields of a single task control block. ISR safe. */
uint32_t KernelVerifyTcb(const void *tcb);

/* Verify taskStatus, stackPointer and priority of every TCB in the table. */
uint32_t KernelVerifyBasic(void);

/* Verify each task's stack magic word and that its saved SP lies inside the stack. */
uint32_t KernelVerifyStack(void);

/* Verify the current SP lies within the stack of the running task. */
uint32_t KernelVerifyCurSp(void);

/* Run Basic, Stack and CurSp in one pass with interrupts disabled. */
uint32_t KernelVerifyAll(void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif
#endif /* _KERNEL_VERIFY_H */

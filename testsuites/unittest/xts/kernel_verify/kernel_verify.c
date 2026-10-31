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

/*
 * kernel_verify probe: detects liteos_m by directly reading TCB fields, the
 * stack magic word and the stack pointer; stack bounds via LOS_TaskInfoGet.
 */

#include "kernel_verify.h"

/* Disable interrupts; caller must pass the returned state to KV_INT_RESTORE. */
static inline uint32_t KV_INT_DISABLE(void)
{
    uint32_t state;
#if defined(__riscv)
    __asm__ volatile(
        "csrr %0, mstatus\n\t"
        "csrci mstatus, 8"
        : "=r"(state) :: "memory");
#elif defined(__aarch64__)
    __asm__ volatile(
        "mrs %0, daif\n\t"
        "msr daifset, 2"
        : "=r"(state) :: "memory");
#elif defined(__arm__) || defined(__thumb__)
    __asm__ volatile(
        "mrs %0, primask\n\t"
        "cpsid i"
        : "=r"(state) :: "memory");
#else
#error "Unsupported architecture: kernel_verify probe requires __riscv, __arm__/__thumb__, or __aarch64__"
#endif
    return state;
}

/* Restore interrupt state saved by KV_INT_DISABLE (re-enables if previously enabled). */
static inline void KV_INT_RESTORE(uint32_t state)
{
#if defined(__riscv)
    __asm__ volatile("csrw mstatus, %0" :: "r"(state) : "memory");
#elif defined(__aarch64__)
    __asm__ volatile("msr daif, %0" :: "r"(state) : "memory");
#elif defined(__arm__) || defined(__thumb__)
    __asm__ volatile("msr primask, %0" :: "r"(state) : "memory");
#else
#error "Unsupported architecture"
#endif
}

static inline uintptr_t KV_GET_SP(void)
{
    uintptr_t sp;
#if defined(__riscv)
    __asm__ volatile("mv %0, sp" : "=r"(sp));
#elif defined(__aarch64__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#elif defined(__arm__) || defined(__thumb__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#else
    sp = (uintptr_t)__builtin_frame_address(0);
#endif
    return sp;
}

uint32_t KernelVerifyTcb(const void *tcb)
{
    uint32_t err = KERNEL_VERIFY_OK;
    uint16_t status;
    uintptr_t sp;
    uint32_t prio;

    if (tcb == NULL) {
        return KERNEL_VERIFY_ERR_ARRAY_NULL;
    }

    /* Status first: an UNUSED slot has an uninitialized stackPointer. */
    status = KV_RD_STATUS(tcb);
    if ((status & ~KV_VALID_MASK) != 0) {
        err |= KERNEL_VERIFY_ERR_STATUS;
    }
    if ((status & KV_STATUS_UNUSED) != 0) {
        /*
         * Free slot: some versions OR the UNUSED bit into leftover status
         * instead of clearing it (persistent UNUSED|RUNNING seen on board);
         * skip the SP and priority checks.
         */
        return err;
    }

    sp = KV_RDSP(tcb);
    if (sp == 0) {
        return err | KERNEL_VERIFY_ERR_SP_NULL;
    }
    if ((sp & (KV_SP_ALIGN - 1)) != 0) {
        err |= KERNEL_VERIFY_ERR_SP_ALIGN;
    }

    prio = KV_RD_PRIO(tcb);
    if (prio > KV_PRIORITY_LOWEST) {
        err |= KERNEL_VERIFY_ERR_PRIO;
    }

    return err;
}

uint32_t KernelVerifyBasic(void)
{
    uint32_t err = KERNEL_VERIFY_OK;
    uint32_t runningCount = 0;
    uint32_t i;

    if (kv_task_table() == NULL) {
        return KERNEL_VERIFY_ERR_ARRAY_NULL;
    }

    for (i = 0; i < KV_TSK_LIMIT; i++) {
        char *tcb = (char *)kv_task_table() + i * KV_TCB_SIZE;
        err |= KernelVerifyTcb(tcb);

        /*
         * Count RUNNING in created tasks only: free slots may carry a stale
         * RUNNING bit on some versions (see the UNUSED note above).
         */
        if ((KV_RD_STATUS(tcb) & (KV_STATUS_RUNNING | KV_STATUS_UNUSED)) == KV_STATUS_RUNNING) {
            runningCount++;
        }
    }
    if (runningCount != 1) {
        err |= KERNEL_VERIFY_ERR_RUNNING_COUNT;
    }

    return err;
}

uint32_t KernelVerifyStack(void)
{
    uint32_t err = KERNEL_VERIFY_OK;
    uint32_t i;

    if (kv_task_table() == NULL) {
        return KERNEL_VERIFY_ERR_ARRAY_NULL;
    }

    for (i = 0; i < KV_TSK_LIMIT; i++) {
        TSK_INFO_S info;
        char *tcb = (char *)kv_task_table() + i * KV_TCB_SIZE;
        uintptr_t top;
        uint32_t size;
        uintptr_t sp;

        if (LOS_TaskInfoGet(i, &info) != LOS_OK) {
            continue;
        }

        top = (uintptr_t)info.uwTopOfStack;
        size = info.uwStackSize;
        sp = KV_RDSP(tcb);

        if ((*(volatile uint32_t *)top) != KV_MAGIC) {
            err |= KERNEL_VERIFY_ERR_BOTTOM_MAGIC;
        }
        if ((sp <= top) || (sp > top + size)) {
            err |= KERNEL_VERIFY_ERR_STACK_RANGE;
        }
    }

    return err;
}

uint32_t KernelVerifyCurSp(void)
{
    uint32_t err = KERNEL_VERIFY_OK;
    uint32_t count = 0;
    char *owner = NULL;
    uintptr_t sp;
    uint32_t i;

    if (kv_task_table() == NULL) {
        return KERNEL_VERIFY_ERR_ARRAY_NULL;
    }

    sp = KV_GET_SP();

    for (i = 0; i < KV_TSK_LIMIT; i++) {
        TSK_INFO_S info;
        char *tcb = (char *)kv_task_table() + i * KV_TCB_SIZE;
        uintptr_t top;
        uint32_t size;

        if (LOS_TaskInfoGet(i, &info) != LOS_OK) {
            continue;
        }

        top = (uintptr_t)info.uwTopOfStack;
        size = info.uwStackSize;
        if ((sp > top) && (sp <= top + size)) {
            count++;
            owner = tcb;
        }
    }

    if (count == 0) {
        return err | KERNEL_VERIFY_ERR_CUR_SP_NO_OWNER;
    }
    if (count > 1) {
        return err | KERNEL_VERIFY_ERR_CUR_SP_MULTI_OWNER;
    }

    if ((KV_RD_STATUS(owner) & KV_STATUS_RUNNING) == 0) {
        err |= KERNEL_VERIFY_ERR_NOT_RUNNING;
    }

    return err;
}

uint32_t KernelVerifyAll(void)
{
    uint32_t err = KERNEL_VERIFY_OK;
    uint32_t state;

    if (kv_task_table() == NULL) {
        return KERNEL_VERIFY_ERR_ARRAY_NULL;
    }

    state = KV_INT_DISABLE();
    err |= KernelVerifyBasic();
    err |= KernelVerifyStack();
    err |= KernelVerifyCurSp();
    KV_INT_RESTORE(state);

    return err;
}

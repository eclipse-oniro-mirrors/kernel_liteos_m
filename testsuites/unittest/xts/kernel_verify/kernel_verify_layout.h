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

#ifndef _KERNEL_VERIFY_LAYOUT_H
#define _KERNEL_VERIFY_LAYOUT_H

#include <stdint.h>

/*
 * Header resolution across liteos versions: los_task_pri.h (OpenHarmony
 * layout) or los_task.h (classic layout); both expose TSK_INFO_S and
 * LOS_TaskInfoGet needed by the stack checks.
 */
#if __has_include("los_task_pri.h")
#include "los_task_pri.h"
#else
#include "los_task.h"
#endif
#include "los_config.h"

/*
 * Task table base via the kernel's own OS_TCB_FROM_TID macro: each version
 * references its own symbol name (g_osTaskCBArray or g_taskCBArray).
 */
static inline LosTaskCB *kv_task_table(void)
{
    return OS_TCB_FROM_TID(0);
}

/*
 * Union of taskStatus bits legal in at least one liteos version, verified on
 * real boards; 0x0200/0x0400 are written by no version. Own constants, not
 * kernel macros: the same macro name can mean different fields per version.
 */
#define KV_STATUS_UNUSED       0x0001u
#define KV_STATUS_SUSPEND      0x0002u
#define KV_STATUS_READY        0x0004u
#define KV_STATUS_PEND         0x0008u
#define KV_STATUS_RUNNING      0x0010u
#define KV_STATUS_DELAY        0x0020u
#define KV_STATUS_TIMEOUT      0x0040u
#define KV_STATUS_PEND_TIME    0x0080u
#define KV_STATUS_ZOMBIE_EXIT  0x0100u  /* ZOMBIE or EXIT depending on version */
#define KV_STATUS_STACK_FREE   0x0800u  /* task stack was freed */
#define KV_STATUS_SYSTEM_TASK  0x1000u  /* software-timer task */
#define KV_STATUS_SIGNAL       0x2000u  /* pending signal */
#define KV_STATUS_FREEZE       0x4000u  /* scheduler frozen */
#define KV_STATUS_JOINABLE     0x8000u  /* created with JOINABLE attribute */

#define KV_VALID_MASK                                                           \
    (KV_STATUS_UNUSED | KV_STATUS_SUSPEND | KV_STATUS_READY |                   \
     KV_STATUS_PEND | KV_STATUS_RUNNING | KV_STATUS_DELAY |                     \
     KV_STATUS_TIMEOUT | KV_STATUS_PEND_TIME | KV_STATUS_ZOMBIE_EXIT |          \
     KV_STATUS_STACK_FREE | KV_STATUS_SYSTEM_TASK | KV_STATUS_SIGNAL |          \
     KV_STATUS_FREEZE | KV_STATUS_JOINABLE)

#if KV_VALID_MASK != 0xF9FF
#error "KV_VALID_MASK must equal 0xF9FF"
#endif

/* Common constants. */
#define KV_PRIORITY_LOWEST   31
#define KV_MAGIC             0xCCCCCCCC /* word liteos writes at each task stack bottom */

/* Config-dependent values. */
#define KV_TCB_SIZE          sizeof(LosTaskCB)
#define KV_TSK_LIMIT         (LOSCFG_BASE_CORE_TSK_LIMIT + 1)

/* SP minimum alignment: at least pointer-size. */
#define KV_SP_ALIGN          sizeof(void *)

/* Offsets of the first three TCB fields, identical across liteos versions. */
#define KV_CONTRACT_SP_OFF       0
#define KV_CONTRACT_STATUS_OFF   4
#define KV_CONTRACT_PRIO_OFF     6

/* Field read macros. */
#define KV_RD16(tcb, off)   (*(volatile uint16_t *)((char *)(tcb) + (off)))
#define KV_RDSP(tcb)        ((uintptr_t)(*(volatile void *const *)(tcb)))
#define KV_RD_STATUS(tcb)   KV_RD16(tcb, KV_CONTRACT_STATUS_OFF)
#define KV_RD_PRIO(tcb)     KV_RD16(tcb, KV_CONTRACT_PRIO_OFF)

#endif /* _KERNEL_VERIFY_LAYOUT_H */

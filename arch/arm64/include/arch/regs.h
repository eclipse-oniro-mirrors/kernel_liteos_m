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

#ifndef _ARCH_REGS_H
#define _ARCH_REGS_H

/*
 * EL-abstracted system register names for the exception entry code
 * (los_exc.S). Only the assembler mapping is provided: the slave core
 * always runs at EL1 (PSCI CPU_ON entry via TF-A, no resident secure
 * monitor), so the LOSCFG_ARCH_SECURE_MONITOR_MODE (EL3) variants are
 * kept for reference and are not selectable in this tree.
 */

#ifdef LOSCFG_ARCH_SECURE_MONITOR_MODE
#define SPSR_ELx   spsr_el3
#define ELR_ELx    elr_el3
#define TPIDR_ELx  tpidr_el3
#define ESR_ELx    esr_el3
#define FAR_ELx    far_el3
#else
#define SPSR_ELx   spsr_el1
#define ELR_ELx    elr_el1
#define TPIDR_ELx  tpidr_el1
#define ESR_ELx    esr_el1
#define FAR_ELx    far_el1
#endif

#ifndef __ASSEMBLER__

/* System register access helpers. Used by the MMU framework
 * (los_mmu_arm64.c) and usable from any C translation unit. */

#define STRINGIFY_1(x)                          #x
#define STRINGIFY(x)                            STRINGIFY_1(x)

#define AARCH64_SYSREG_READ(reg)                                  \
    ({                                                            \
        UINT64 _val;                                              \
        __asm__ volatile("mrs %x0," STRINGIFY(reg) : "=r"(_val)); \
        _val;                                                     \
    })

#define AARCH64_SYSREG_WRITE(reg, val)                             \
    ({                                                             \
        __asm__ volatile("msr " STRINGIFY(reg) ", %x0" ::"r"((UINT64)val)); \
        __asm__ volatile("isb" ::: "memory");                      \
    })

#endif /* __ASSEMBLER__ */

#endif /* _ARCH_REGS_H */

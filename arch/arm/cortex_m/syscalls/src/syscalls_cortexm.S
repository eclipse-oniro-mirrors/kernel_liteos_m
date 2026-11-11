#include "arch/task_box_cb.h"

#ifndef SYSCALL_CORTEXM_S
#define SYSCALL_CORTEXM_S
#ifdef  __IASMARM__

    PRESERVE8
    SECTION    .usr:CODE(2)
    THUMB
    IMPORT g_sysMode
    IMPORT g_runTask
    IMPORT GetSyscallNestCntAddr

OsSyscallKernelEntry:
    push   {r0, r1}
    MOV32  r1, g_runTask
    ldr    r0, [r1]
    mov    r1, #2
    str    r1, [r0, #TASK_CB_SYSCALL_NEST_CNT]
    pop    {r0, r1}
    bx     r12

OsSyscallUserEntryNoAlign:
    sub    sp, sp, #4
    /* Bit 0 indicates whether the svc stack is aligned. */
    orr    r12, r12, #0x1
    svc    255
    add    sp, sp, #4
    bx     lr

OsSyscallUserEntry:
    svc    255
    bx     lr
FNEND

SYSCALL MACRO
    PRESERVE8
    SECTION     .syscall_func_text:CODE(2)
    THUMB
    EXPORT \3
    IMPORT SYS_\3
\3:
    mrs    r12, ipsr
    cmp    r12, #0
    bne    in_kernel_\3
    MOV32  r12, g_sysMode
    ldr    r12, [r12]
    cmp    r12, #0
    beq    in_kernel_\3
    /* Ensure that sp and 8 bytes are aligned. */
    mov    r12, sp
    tst    r12, #0x7
    mov32  r12, SYS_\3_ADDR
    beq    OsSyscallUserEntry
    b      OsSyscallUserEntryNoAlign
in_kernel_\3:
    mov32  r12, SYS_\3
    b      OsSyscallKernelEntry

    SECTION .syscall_addr_array:CODE(2)
    EXPORT  SYS_\3_ADDR
    DATA
SYS_\3_ADDR DC32    SYS_\3
    ENDM

SYSCALL_DEFTEST MACRO n, ret, name
    ENDM

#define LITEOS_SYSCALL_DEF   SYSCALL
#define LITEOS_SYSCALL_RSV   SYSCALL_DEFTEST
#include "syscalls_def.h"

#ifdef __IASMARM__
    END
#endif

#else /* __IASMARM__ */
.syntax unified
.cpu cortex-m4
.fpu softvfp
.thumb

.section .usr.text
.macro SYSCALL name, n
.global \name
.global g_sysMode
.type \name, %function
.fnstart
\name:
    push   {r0 - r3}
    mrs    r1, ipsr
    cmp    r1, #0
    bne    no_\name
    ldr    r1, =g_sysMode
    ldr    r1, [r1]
    cmp    r1, #0
    beq    no_\name
    mov    r0, sp
    svc    \n
    pop    {r0 - r3}
    bx     lr
no_\name:
    pop    {r0 - r3}
    b      SYS_\name
.pool
.fnend
.endm

#define LITEOS_SYSCALL_DEF(n, ret, name, arg...)        SYSCALL     name, n

#include "syscalls_def.h"

#endif
#endif

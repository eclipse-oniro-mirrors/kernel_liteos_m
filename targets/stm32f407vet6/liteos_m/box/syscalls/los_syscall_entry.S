/*
 * los_syscall_entry.S — user-side syscall veneers.
 *
 * Faithful port of the GCC branch of refs syscalls_cortexm.S
 * (.macro SYSCALL name, n): push args, three-way mode check, svc #num,
 * pop args (retval arrives in r0 via the user-stack args[0] slot written
 * by the kernel epilogue).
 *
 * Naming (refs syscalls_cortexm.S: EXPORT \name / IMPORT SYS_\name):
 * the veneer carries the PLAIN user-facing name (LOS_xxx) in .usr.text,
 * and the kernel/ISR direct-call tail branches to SYS_<name> - the
 * renamed kernel implementation. User code links LOS_xxx to this veneer;
 * kernel TUs see LOS_xxx macro-renamed to SYS_LOS_xxx via los_base.h.
 *
 * Section .usr.text -> COMMON_FLASH (MPU R0): user-executable by design.
 */
.syntax unified
.cpu cortex-m4
.fpu softvfp
.thumb

    .section .usr.text, "ax"
    .thumb

    .extern g_sysMode

.macro SYSCALL name, num
    .global \name
    .type  \name, %function
    .fnstart
\name:
    push    {r0-r3}
    mrs     r1, ipsr
    cmp     r1, #0
    bne     1f                          /* Handler mode (ISR): direct call */
    ldr     r1, =g_sysMode
    ldr     r1, [r1]
    cmp     r1, #0
    beq     1f                          /* kernel thread: direct call     */
    mov     r0, sp                      /* user path: r0 = args pointer   */
    svc     \num
    pop     {r0-r3}                     /* r0 = retval (args[0] slot)     */
    bx      lr
1:
    pop     {r0-r3}
    b       SYS_\name                   /* tail call to the SYS_ implementation */
    .pool
    .fnend
.endm

#define LITEOS_SYSCALL_DEF(num, ret, name, args...) SYSCALL name, num
#include "syscalls_num.h"
#include "syscalls_def.h"

/*
 * OsUsrTaskExit — user task exit trampoline (refs task.c: OsTskUserStackInit
 * sets uwLR = OsUsrTaskExit, thumb bit set). The initial LR of every box
 * entry task points here: a plain return from the entry function terminates
 * the task instead of branching to address 0. The veneer protocol is
 * inlined (push block + r0 = sp + svc) because this runs in user mode
 * exactly like a veneer; the svc never returns - the exit syscall deletes
 * the current task - so the loop below is purely defensive.
 */
    .section .usr.text.OsUsrTaskExit, "ax"
    .thumb
    .global OsUsrTaskExit
    .type  OsUsrTaskExit, %function
    .thumb_func
    .fnstart
OsUsrTaskExit:
    movs    r0, #0                     /* exit code */
    push    {r0-r3}
    mov     r0, sp                     /* r0 = args pointer */
    svc     SYSCALL_NUM_TASK_USR_EXIT
1:
    b       1b                         /* unreachable: exit never returns */
    .fnend

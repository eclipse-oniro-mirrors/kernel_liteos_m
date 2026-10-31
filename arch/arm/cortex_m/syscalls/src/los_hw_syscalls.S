#include "arch/macro.h"
#ifdef __IASMARM__
    PRESERVE8
#else
    .syntax unified
    .arch armv7-m
    .thumb
    .fpu vfpv4
#endif

#include "arch/task_box_cb.h"
#include "box_mpu_config.h"

    EXTERNAL OsSyscallHandler
    EXTERNAL g_sysMode
    EXTERNAL g_runTask

    EQUAL EXC_xPSR_NE              , 0x01000000
    EQUAL EXC_xPSR_E               , 0x01000200
    EQUAL EXC_LR                   , 0xFFFFFFFD
    EQUAL EXC_FPU_LR               , 0xFFFFFFED
    EQUAL FPCCR                    , 0xE000EF34
    EQUAL FPCCR_RESET              , 0xFFFFFFFE

#ifdef __IASMARM__
    SECTION     .usr:CODE(2)
    THUMB
#else
    .section    .usr.text
#endif
    FUNC OsSyscallEpilogue
OsSyscallEpilogue:
    FNSTART
    CANTUNWIND
    mrs    r12, primask
    cpsid  I
    /* Update the user kernel stack. */
    ldr    r2, =g_runTask
    ldr    r2, [r2]
    add    sp, sp, #16
    str    sp, [r2, #TASK_CB_KERNEL_SP]

    /*change psp reg back to user stack */
    ldr    sp, [r2, #TASK_CB_USER_SP]

    /* Change task status to running in user space */
    ldrh   r3, [r2, #TASK_CB_STATUS]
    orr    r3, r3, #OS_TASK_STATUS_INUSER
    strh   r3, [r2, #TASK_CB_STATUS]
    mov    r3, #0
    str    r3, [r2, #TASK_CB_SYSCALL_NEST_CNT]

    /* update g_sysMode */
    ldr    r2, =g_sysMode
    mov    r3, #1
    str    r3, [r2]

    /* Get stored svc exc_return code */
    pop    {r2}

    /* Calculate stack frame size, Determining whether there is a floating-point stack. */
    tst    r2, #0x10
    ite    eq
    moveq  r3, #104   /* 104:sizeof(svcContext(exclude excReturn)) + sizeof(fpuFrameContext) */
    movne  r3, #32       /* 32:sizeof(svcContext(exclude excReturn)) */

    /* Recover LR & Get stored PC reg to R2 */
    ldr    lr, [sp, #20]
    ldr    r2, [sp, #24]

    add    sp, sp, r3

    /* switch the privilege to unprivileged mode and Resume interrupt */
    mrs    r3, control
    orr    r3, r3, #1
    msr    primask, r12
    msr    control, r3

    mov    pc, r2
    FNEND

#ifdef __IASMARM__
    SECTION     .kernel:CODE(2)
    THUMB
#else
    .section    .kernel.text
#endif
    FUNC OsExcSvcCall
OsExcSvcCall:
    FNSTART
    CANTUNWIND
    /* switch the privilege to kernel mode and update g_sysMode */
    mrs    r2, control
    bic    r2, r2, #1
    msr    control, r2
    ldr    r1, =g_sysMode
    mov    r2, #0
    str    r2, [r1]

    /* store exc_return code to the user stack */
    mrs    r12, psp
    sub    r1, r12, #4
    str    lr, [r1]

    /* Update user stack psp addr */
    ldr    r2, =g_runTask
    ldr    r2, [r2]       /* load g_losTask.runTask addr to r2 */
    str    r1, [r2, #TASK_CB_USER_SP]

    /* Change task status to running in kernel */
    ldrh   r1, [r2, #TASK_CB_STATUS]
    bic    r1, r1, #OS_TASK_STATUS_INUSER
    strh   r1, [r2, #TASK_CB_STATUS]
    mov    r1, #1
    str    r1, [r2, #TASK_CB_SYSCALL_NEST_CNT]

    /* change psp reg and stack ptr to kernel stack */
    ldr    lr, [r2, #TASK_CB_KERNEL_SP]

    /* Obtain the handle */
    ldr    r3, [r12, #16]

    /* config FPCCR for bx EXC_RETURN to 0xFFFFFFFE clear FPCCR.LSPACT */
    ldr    r1, =FPCCR
    ldr    r0, [r1]
    and    r2, r0, #FPCCR_RESET
    str    r2, [r1]
    tst    r0, #1
    it     ne
    addne  r12, r12, #72
    add    r12, r12, #32

    /* Checking whether the svc stack is aligned. */
    tst    r3, #1
    it     ne
    addne  r12, r12, #4

    sub    lr, lr, #16
    ldmia  r12,{r0-r3}
    stmia  lr, {r0-r3} /* Construct r4-r7 parameters */

    mrs    r12, psp

    tst    lr, #0x7    /* Check double word alignment */
    beq    kernel_sp_is_align
    sub    lr, lr, #36 /* 36 sizeof(svcContext(exclude excReturn)) + align size 4 */
    ldr    r1, =EXC_xPSR_E
    b      exit_handle

kernel_sp_is_align:
    /* 32: Four parameters(After the fourth param) + sizeof(svcContext(exclude excReturn)) */
    sub    lr, lr, #32
    ldr    r1, =EXC_xPSR_NE
exit_handle:
    ldr    r0, [r12, #-4]
    tst    r0, #0x10  /* check FPU status, Saves the FPU status before the system call. */
    it     eq
    subeq  lr, lr, #72

    push   {r0, lr}

    str    r1, [lr, #28] /* svcContext->xpsr */

    /* Construct the first four parameters. */
    ldmia  r12,{r0-r3}
    stmia  lr, {r0-r3}
    ldr    r1, =OsSyscallEpilogue
    str    r1, [lr, #20]

    /* Obtaining the system call handle */
    ldr    r0, [r12, #16]
    bl     OsSyscallHandler
    pop    {r1, lr}
    str    r0, [lr, #24] /* svcContext->pc */
    msr    psp, lr
    mov    lr, r1
    bx     lr
    FNEND

#ifdef __IASMARM__
    END
#endif
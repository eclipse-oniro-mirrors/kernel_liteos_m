#include "arch/macro.h"
#include "arch/task_box_cb.h"

    EXTERNAL g_sysMode
    EXTERNAL g_runTask
#ifdef __IASMARM__
    SECTION    .usr:CODE(2)
#else
    .section   .usr.text.OsUserspaceEnter
#endif
    FUNC OsUserspaceEnter
OsUserspaceEnter:
    FNSTART
    cpsid   I
    ldr     r4, =g_runTask
    ldr     r4, [r4]
    /* Change task status to running in userspace */
    ldrh    r5, [r4, #TASK_CB_STATUS]
    ldr     r6, =OS_TASK_STATUS_INUSER
    orr     r5, r5, r6
    strh    r5, [r4, #TASK_CB_STATUS]

    /* Retrieve the user stack pointer */
    ldr     r5, [r4, #TASK_CB_USER_SP]
    msr     psp, r5
    add     sp, sp, #36

#if defined(__VFP_FP__) && !defined(__SOFTFP__) || defined(__ARMVFP__)
    add     sp, sp, #4
#endif

    ldr     r5, =g_sysMode
    mov     r4, #1
    str     r4, [r5]

    cpsie   I
    movs    r4, #3    // Use psp and running in unprivileged mode
    msr     control, r4

    add     sp, sp, #16
    pop     {r4-r7}

    mov     lr, r5
    bx      r6
    FNEND

#ifdef __IASMARM__
    END
#endif
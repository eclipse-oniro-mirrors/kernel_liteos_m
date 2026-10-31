
#include "arch/macro.h"
#include "box_mpu_config.h"
#ifdef __IASMARM__
    PRESERVE8
    SECTION     .text:CODE(2)
    THUMB
#else
    .syntax unified
    .arch   armv7e-m
    .section .text
    .thumb
#endif

#include "arch/task_box_cb.h"

    EXTERNAL g_boxLocalPool
    EXTERNAL g_curBoxId
    EXTERNAL g_perboxDataMap
    EXTERNAL g_curPerBoxData
#ifdef LOSCFG_EXC_ISOLATION
    EXTERNAL g_excSaveMsp
#endif
    EXTERNAL g_oldTask
    EXTERNAL g_runTask
    EXTERNAL OsSchedTaskSwitch
    EXTERNAL g_boxCBArr
    EXTERNAL g_sysMode
   


    EQUAL OS_NVIC_INT_CTRL,           0xE000ED04
    EQUAL OS_NVIC_SYSPRI2,            0xE000ED20
    EQUAL OS_NVIC_PENDSV_PRI,         0xF0F00000
    EQUAL OS_NVIC_PENDSVSET,          0x10000000
    EQUAL RECOV_LR,                   0xFFFFFFFD

    EQUAL DRVBX_STAT_SWITCH,          0x01
    EQUAL MPU_CTRL,                   0xE000ED94
    EQUAL MPU_RNR,                    0xE000ED98
    EQUAL MPU_RABR,                   0xE000ED9C
    EQUAL MPU_RASR,                   0xE000EDA0
    EQUAL MPU_RBAR_VALID,             0x10
    EQUAL MPU_RBAR_SHMEM_VALID,       0x16
    EQUAL MPU_EN_WITH_PRIVDEFENA,     0x05
#ifdef LOSCFG_BASE_IPC_SHMEM
    EQUAL MPU_START_REGION,           6
#else
    EQUAL MPU_START_REGION,           5
#endif

    EQUAL HIGHEST_BASEPRI_VALUE,      0x80

    FUNC OsStartToRun
OsStartToRun:
    FNSTART
    CANTUNWIND
    ldr     r4, =OS_NVIC_SYSPRI2
    ldr     r5, =OS_NVIC_PENDSV_PRI
    str     r5, [r4]

    ldr     r1, =g_oldTask
    str     r0, [r1]

    ldr     r1, =g_runTask
    str     r0, [r1]

    movs    r1, #2
    msr     control, r1

    movs    r6, #OS_TASK_STATUS_RUNNING
    strh    r6, [r0, #TASK_CB_STATUS]

    ldr     r12, [r0]

    add     r12, r12, #36
    /* __VFP_FP__ is set by -mfpu; __SOFTFP__ is set by -mfloat-abi=soft. */
#if defined(__VFP_FP__) && !defined(__SOFTFP__) || defined(__ARMVFP__)
    add     r12, r12, #4
#endif

    ldmfd   r12!, {R0-R7}

    msr     psp, r12

    mov     lr, r5

    cpsie   I
    bx      r6
    FNEND

    FUNC OsTaskSchedule
OsTaskSchedule:
    FNSTART
    CANTUNWIND
    ldr     r2, =OS_NVIC_INT_CTRL
    ldr     r3, =OS_NVIC_PENDSVSET
    str     r3, [r2]
    /* if in thread mode, save the basepri, if in handle mode, no need to save */
    mov     r2, #0
    mrs     r3, ipsr
    cbnz    r3, OsTaskScheduleExit
    mrs     r0, basepri
    msr     basepri, r2
    cpsie   i
    msr     basepri, r0
OsTaskScheduleExit:
    bx      lr
    FNEND

    FUNC SwitchNewTask
SwitchNewTask:
#ifdef LOSCFG_EXC_ISOLATION
    ldr     r0, =g_excSaveMsp
    ldr     r0, [r0]
    mov     sp, r0
#endif
    ldr     r0, =RECOV_LR
    mov     lr, r0
    ldr     r1, =g_oldTask
    mov     r0, #0
    msr     basepri, r0
    b       SwitchNewTaskTag

    FUNC OsPendSV
OsPendSV:
    FNSTART
    CANTUNWIND
TaskSwitch:
    mrs       r0, psp
#if defined(__VFP_FP__) && !defined(__SOFTFP__) || defined(__ARMVFP__)
    /* when enter the exc or interrupt, lr's value is EXC_RETURN. If FPCA = 1, the 4's bit of EXC_RETURN is 0. */
    tst       lr, #0x10
    it        eq
    vstmdbeq  r0!, {d8-d15}              /* push VFP registers. */   
    stmfd     r0!, {r14}
#endif
    stmfd     r0!, {r4-r12}

    ldr       r1, =g_oldTask
    ldr       r7, [r1]
    ldrh      r2, [r7, #TASK_CB_STATUS]
    tst       r2, #OS_TASK_STATUS_INUSER    /* whether the old task is in user mode */
    beq       OldTaskInKernel               /* old task in kernel mode, jump */
    str       r0, [r7, #TASK_CB_USER_SP]    /* save usr sp, mpu config can't save on user stack */
    ldr       r0, [r7]                      /* load kernel sp */
OldTaskInKernel:
    str       r0, [r7]                      /* save kernel sp to old taskCB */

SwitchNewTaskTag:
    ldr       r0, =g_runTask
    ldr       r0, [r0]
#ifdef LOSCFG_SCHED_IN_PENDSV
#if defined(LOSCFG_BASE_CORE_TSK_MONITOR) || defined(LOSCFG_KERNEL_TRACE)
    push      {r0, r1, lr}
    ldr       r1, [r1]
    bl        OsSchedTaskSwitch              /* r0 = newTask; r1 = runTask */
    pop       {r0, r1, lr}
#endif
#endif

    /* update g_curBoxId */
    ldrb      r4, [r0, #0x29]
    ldr       r3, =g_curBoxId
    str       r4, [r3]                      

    /* g_oldTask = g_runTask */
    ldr       r7, [r1]
    str       r0, [r1]
    ldr       r1, [r0]                       /* load kernel sp */

    /* if in the same box, tasks needn't config pmp */
    ldr       r2, [r0, #TASK_CB_BOX_CB]
    cmp       r2, #0
    bne       isBox          /* new task is user task, jump */
    KERNEL_DATA_ENABLE  r2, r3
    b         modeConfig     /* new task is kernel task, jump */
isBox:
    ldr       r3, [r7, #TASK_CB_BOX_CB]
    cmp       r2, r3
    beq       modeConfig
boxConfig:
    BOX_DATA_ENABLE r2, r3, r4

    /* g_curPerBoxData = g_perboxDataMap[curBoxId] */
    ldrb      r4, [r0, #0x29]
    ldr       r3, =g_perboxDataMap
    ldr       r4, [r3, r4, lsl #2]
    ldr       r3, =g_curPerBoxData
    str       r4, [r3]

    /* set g_boxLocalPool */
    BOX_MEM_POOL_UPDATE r2, r3, r4

modeConfig:
    ldrh      r2, [r0, #TASK_CB_STATUS]
    mrs       r3, control
    bic       r3, r3, #1
    tst       r2, #OS_TASK_STATUS_INUSER

    ittt      ne
    strne     r1, [r0]                      /* save kernel sp */
    ldrne     r1, [r0, #TASK_CB_USER_SP]    /* load usr sp */
    orrne     r3, r3, #1                    /*in user mode, set control's bit0 to 1 */

    msr       control, r3

    and       r3, r3, #1
    ldr       r2, =g_sysMode
    str       r3, [r2]

LoadNormalRegister:
    ldmfd     r1!, {r4-r12}

#if defined(__VFP_FP__) && !defined(__SOFTFP__) || defined(__ARMVFP__)
    ldmfd     r1!, {r14}
    tst       r14, #0x10
    it        eq
    vldmiaeq  r1!, {d8-d15}
#endif

    msr       psp, r1
    bx        lr
    FNEND
#ifdef __IASMARM__
    END
#endif
/*
 * STM32F407VET6 startup, adapted from arm_mps2_an386:
 * - image runs from flash, so .data is copied flash -> ram before main
 * - vector table keeps only the 16 system entries: the kernel only needs
 *   PendSV/SysTick/fault handlers, no external IRQ is used by this demo
 * - clock stays at the reset default (HSI 16MHz), no SystemInit needed
 */

    .syntax unified
    .cpu cortex-m4
    .fpu softvfp
    .thumb

    .extern HalExcNMI;
    .extern HalExcHardFault;
    .extern HalExcMemFault;
    .extern HalExcBusFault;
    .extern HalExcUsageFault;
    .extern HalExcSvcCall;
    .extern HalPendSV;
    .extern SysTick_Handler;
    .global Vector_Table
    .global Reset_Handler

    .section .text
    .type  Reset_Handler, %function
Reset_Handler:
    /* copy .data (incl. init_array / sysinitcall) from flash to ram */
    ldr  r0, =__data_load
    ldr  r1, =__data_start
    ldr  r2, =__data_end

data_loop:
    cmp  r1, r2
    bcs  data_done
    ldr  r3, [r0, #0]
    str  r3, [r1, #0]
    add  r0, r0, #4
    add  r1, r1, #4
    b    data_loop
data_done:

    /* copy .common_data (MPU R1: common RW) from flash to ram */
    ldr  r0, =__common_data_load
    ldr  r1, =__common_data_start
    ldr  r2, =__common_data_end

common_data_loop:
    cmp  r1, r2
    bcs  common_data_done
    ldr  r3, [r0, #0]
    str  r3, [r1, #0]
    add  r0, r0, #4
    add  r1, r1, #4
    b    common_data_loop
common_data_done:

    /* copy .userrd_data (MPU R2: user read-only) from flash to ram */
    ldr  r0, =__user_readonly_data_load
    ldr  r1, =__user_readonly_data_start
    ldr  r2, =__user_readonly_data_end

userrd_data_loop:
    cmp  r1, r2
    bcs  userrd_data_done
    ldr  r3, [r0, #0]
    str  r3, [r1, #0]
    add  r0, r0, #4
    add  r1, r1, #4
    b    userrd_data_loop
userrd_data_done:

    /* zero .bss */
    ldr  r0, =__bss_start
    ldr  r1, =__bss_end
    movs r2, #0

bss_loop:
    cmp  r0, r1
    bcs  bss_done
    str  r2, [r0, #0]
    add  r0, r0, #4
    b    bss_loop
bss_done:

    /* zero .common_bss (MPU R1) */
    ldr  r0, =__common_bss_start
    ldr  r1, =__common_bss_end
    movs r2, #0

common_bss_loop:
    cmp  r0, r1
    bcs  common_bss_done
    str  r2, [r0, #0]
    add  r0, r0, #4
    b    common_bss_loop
common_bss_done:

    /* zero .userrd_bss (MPU R2) */
    ldr  r0, =__user_readonly_bss_start
    ldr  r1, =__user_readonly_bss_end
    movs r2, #0

userrd_bss_loop:
    cmp  r0, r1
    bcs  userrd_bss_done
    str  r2, [r0, #0]
    add  r0, r0, #4
    b    userrd_bss_loop
userrd_bss_done:

    ldr  sp, =__irq_stack_top
    b    main
    .size Reset_Handler, .-Reset_Handler

    .type  DefaultHandler, %function
DefaultHandler:
loop:
    b    loop
    .size DefaultHandler, .-DefaultHandler

    .section .BOOT_VECTOR_TABLE,"a",%progbits
    .type  Vector_Table, %object
Vector_Table:
    .word _estack
    .word Reset_Handler
    .word HalExcNMI
    .word HalExcHardFault
    .word HalExcMemFault
    .word HalExcBusFault
    .word HalExcUsageFault
    .word 0
    .word 0
    .word 0
    .word 0
    .word DefaultHandler
    .word DefaultHandler
    .word 0
    .word HalPendSV
    .word SysTick_Handler

    .size Vector_Table, .-Vector_Table

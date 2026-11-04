/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: WS63 RISC-V startup code (adapted from 2.08.5 reset_vector.S)
 *
 * Changes from 2.08.5:
 *   - TrapVector -> HalTrapVector (liteos_m trap entry)
 *   - FPU enable restored (matches SDK 2.08.5; LOSCFG_ARCH_FPU_DISABLE not set)
 *   - Symbol names adapted: __stack_top__ -> __start_and_irq_stack_top,
 *     __data_load__/__data_begin__/__data_end__ -> __data_load_start/__data_start/__data_end,
 *     __bss_begin__/__bss_end__ -> __bss_start/__bss_end
 *   - Section .text.entry -> .start.text (liteos_m convention)
 *
 * Entry: reset_vector (matches ENTRY(reset_vector) in board.ld)
 */

    .section .start.text, "ax"
    .global reset_vector
    .option norvc

reset_vector:
    /* 1. Configure SRAM sharing — map RAM blocks to ITCM/DTCM */
    li      t0, 0x44006c04
    lw      t1, 0(t0)
    li      t2, 0xFF0003FF
    and     t1, t1, t2
    sw      t1, 0(t0)
    /* ram10=DTCM(3), ram11=ITCM(2), rest=pkt_ram(0) */
    li      t0, 0x44006c08
    lw      t1, 0(t0)
    li      t2, 0xFFFFC003
    and     t1, t1, t2
    li      t2, 0x38
    or      t1, t1, t2
    sw      t1, 0(t0)
    li      t0, 0x44006c04
    lw      t1, 0(t0)
    li      t2, 0xFFFC00
    or      t1, t1, t2
    sw      t1, 0(t0)

    /* 2. Set trap vector to HalTrapVector (liteos_m interrupt/exception entry) */
    la      t0, HalTrapVector
    csrw    mtvec, t0

    /* 3. Clear mstatus / mie */
    csrwi   mstatus, 0
    csrwi   mie, 0

    /* 3.5. Enable FPU (mstatus.FS = Dirty, clear fcsr) — required because
     *      LOSCFG_ARCH_FPU_DISABLE is not set, so TaskContext contains FPU
     *      registers and POP_ALL_REG executes flw/fscsr on task switch.
     *      Without this, the first ArchStartToRun triggers an illegal
     *      instruction trap. Matches SDK 2.08.5 reset_vector.S. */
    li      t0, 0x00006000
    csrs    mstatus, t0
    fscsr   x0

    /* 4. Initialize global pointer and stack pointer */
    la      gp, _gp_
    la      sp, __start_and_irq_stack_top

    /* 5. Clear DTCM (in case anything accesses it) */
    li      t0, 0x180000
    li      t1, 0x184000
1:
    sw      zero, (t0)
    addi    t0, t0, 4
    blt     t0, t1, 1b

    /* 6. Copy .data from flash (LMA) to SRAM (VMA) */
    la      t0, __data_load_start
    la      t1, __data_start
    la      t2, __data_end
    beq     t1, t2, 3f
2:
    lw      t3, (t0)
    sw      t3, (t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    blt     t1, t2, 2b

    /* 7. Clear .bss section */
3:
    la      t0, __bss_start
    la      t1, __bss_end
    beq     t0, t1, 5f
4:
    sw      zero, (t0)
    addi    t0, t0, 4
    blt     t0, t1, 4b
5:
    /* Jump to C main() */
    j       main

    .size   reset_vector, . - reset_vector

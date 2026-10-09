/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 RISC-V startup code
 *
 * Entry: reset_vector (matches ENTRY(reset_vector) in board.ld).
 * SSB/seliteos copies this (.start.text, first 0x800 bytes of the image)
 * from flash 0x260C0300 into ITCM 0x10000 and jumps here. The rest of the
 * code runs XIP from flash; data/bss live in DTCM.
 *
 * Uses `la` (PC-relative) and GP relaxation just like hs-fbb. The board's
 * board.ld defines __global_pointer$ and the link command passes -Wl,--relax-gp,
 * so lld resolves `la symbol` to auipc+addi (PC-rel) and additionally relaxes
 * it to addi gp,off when symbol is within ±2KB of __global_pointer$.
 */

#include "soc.h"

    .extern HalTrapVector
    .section .start.text, "ax"
    .global reset_vector
    .option norvc

/*
 * Early-debug UART marker macro: writes a single ASCII char to UART0
 * (data reg 0x5200B004, status 0x5200B044, TX_FULL bit0) WITHOUT touching
 * .data/.bss. Uses t0/t1 as scratch — call only where those are spare.
 * Helpful for narrowing down where in reset_vector a trap occurs: if we
 * see "R0" but not "R1", the trap is between the two markers.
 */
.macro EARLY_PUTC ch
    li      t0, UART0_BASE
1:
    lw      t1, UART0_FIFO_STATUS_OFF(t0)
    andi    t1, t1, (1 << UART_TX_FIFO_FULL_BIT)
    bnez    t1, 1b
    li      t1, \ch
    sw      t1, UART0_DATA_OFF(t0)
.endm

reset_vector:
_start:
    /*
     * Disable the watchdog very early so it cannot reset the board while the
     * app initializes (the SSB/bootrom may have left one running). WDT V151
     * at 0x57020000: unlock then clear CR.
     */
    li      t0, WDT_BASE
    li      t1, WDT_KEY
    sw      t1, 0(t0)
    sw      zero, WDT_CR_OFF(t0)

    EARLY_PUTC 'R'                  /* marker: reset_vector reached */
    EARLY_PUTC '0'
    EARLY_PUTC '\r'
    EARLY_PUTC '\n'

    /* trap & interrupt vectors */
    la      t0, HalTrapVector
    csrw    mtvec, t0

    /*
     * CLIC mtvt — match hs-fbb: set mtvt to g_vectorIntTable. We use
     * non-vector mode (clicintattr.bit0=0), so mtvt is unused at runtime,
     * but it's safer to point it at a valid 64-byte-aligned table than
     * leave whatever seliteos passed.
     */
    la      t0, g_vectorIntTable
    csrw    mtvt, t0

    la      t0, __irq_stack_top
    csrw    mscratch, t0
    /*
     * Match hs-fbb: clear mstatus entirely (clears MPP/MPIE/MIE/FS/etc),
     * then re-enable FS later. Our previous "csrci mstatus, 0x8" kept
     * seliteos's leftover MPP/MPIE which could affect later mret.
     */
    csrwi   mstatus, 0
    li      t0, 0x00006000
    csrs    mstatus, t0
    csrwi   mie, 0

    .option push
    .option norelax
    /* initialize global pointer & stack pointer (in DTCM) */
    la      gp, __global_pointer$
    la      sp, __start_and_irq_stack_top
    .option pop

    /* clear pmpaddr20 (PMP_INDEX_BOOTROM_ADDR) left by bootrom/flashboot */
    li      t0, 0x0
    csrrw   x0, PMPADDR20_CSR, t0
    /* clear pmpaddr21 (PMP_INDEX_ITCM_TEE_ADDR) left by bootrom/flashboot */
    li      t0, 0x0
    csrrw   x0, PMPADDR21_CSR, t0

    /* config itcm and dtcm background as rw (pmpcfg5 = entries 20-23) */
    li      t0, PMP_CFG_RW_RW
    csrrw   x0, PMPCFG5_CSR, t0

    EARLY_PUTC 'R'                  /* marker: PMP background set */
    EARLY_PUTC '1'
    EARLY_PUTC '\r'
    EARLY_PUTC '\n'

    /* copy .data from flash (LMA) to DTCM (VMA) */
    la      t0, __data_load_start
    la      t1, __data_start
    la      t2, __data_end
    beq     t1, t2, 2f
1:
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    blt     t1, t2, 1b
2:

    EARLY_PUTC 'R'                  /* marker: .data copied */
    EARLY_PUTC '2'
    EARLY_PUTC '\r'
    EARLY_PUTC '\n'

    /*
     * copy .itcm_resident from flash (LMA) to ITCM (VMA): SFC flash
     * write/erase code must run outside the XIP window
     */
    la      t0, __itcm_load_start
    la      t1, __itcm_start
    la      t2, __itcm_end
    beq     t1, t2, 22f
21:
    lw      t3, 0(t0)
    sw      t3, 0(t1)
    addi    t0, t0, 4
    addi    t1, t1, 4
    blt     t1, t2, 21b
22:

    EARLY_PUTC 'R'                  /* marker: .itcm copied */
    EARLY_PUTC '3'
    EARLY_PUTC '\r'
    EARLY_PUTC '\n'

    /* clear .bss in DTCM */
    la      t0, __bss_start
    la      t1, __bss_end
    beq     t0, t1, 4f
3:
    sw      zero, 0(t0)
    addi    t0, t0, 4
    blt     t0, t1, 3b
4:

    /* config itcm background as rx (keep dtcm rw) */
    li      t0, PMP_CFG_RX_RW
    csrrw   x0, PMPCFG5_CSR, t0

    EARLY_PUTC 'R'                  /* marker: about to jump */
    EARLY_PUTC '4'
    EARLY_PUTC '\r'
    EARLY_PUTC '\n'

    /*
     * jump to main (long jump: main is XIP in flash, out of JAL range).
     * `la` resolves to auipc+addi (PC-rel), giving a 32-bit absolute-style
     * address suitable for jr — same effect as the old lui+addi pattern.
     */
    la      t0, main
    jr      t0

    .size   reset_vector, . - reset_vector

/*
 * Switch from EL2/EL3 down to EL1 (AArch64). Ported from hs-fbb
 * arch_lite/arm64/src/el_switch.S (non-secure, IRQ/FIQ routed to EL1).
 *
 * Register usage: x0-x1 scratch. Returns to the caller (LR) at EL1h.
 * Must be called before any stack use is relied upon.
 */

    .arch armv8-a
    .text
    .align 5

    .global arm64_elX_to_el1
    .type   arm64_elX_to_el1, %function
arm64_elX_to_el1:
    mrs     x1, CurrentEL
    cmp     x1, #(0b01 << 2)
    bne     notEL1
    /* Already in EL1 */
    ret

notEL1:
    cmp     x1, #(0b10 << 2)
    beq     inEL2

    /* entered at EL3: route to EL1, IRQ/FIQ not trapped to EL3 */
    mrs     x1, scr_el3
    orr     x1, x1, #(1 << 10)     /* RW: EL1 in AArch64 state */
    bic     x1, x1, #(1 << 2)      /* FIQ not taken to EL3 */
    bic     x1, x1, #(1 << 1)      /* IRQ not taken to EL3 */
    msr     scr_el3, x1
    msr     elr_el3, x30
    mov     x1, #((0b1111 << 6) | (0b0101)) /* EL1h, DAIF masked */
    msr     spsr_el3, x1
    b       confEL1

inEL2:
    msr     elr_el2, x30
    mov     x1, #((0b1111 << 6) | (0b0101)) /* EL1h, DAIF masked */
    msr     spsr_el2, x1

confEL1:
    /* disable EL2 coprocessor/SIMD/FPU traps */
    mov     x0, #0x33ff
    msr     cptr_el2, x0

    /* set EL1 to 64bit, virtualization disabled */
    mov     x0, #(1 << 31)
    msr     hcr_el2, x0

    /* GICv3: allow EL1 system-register (ICC_*) access. Without
     * ICC_SRE_EL2.SRE/ENBLE every ICC_*_EL1 access at EL1 traps. */
    mrs     x0, icc_sre_el2
    orr     x0, x0, #0x1            /* ICC_SRE_EL2.SRE */
    orr     x0, x0, #0x8            /* ICC_SRE_EL2.ENBLE */
    msr     icc_sre_el2, x0

    /* generic timer: allow EL1 access to the physical timer/counters */
    mrs     x0, cnthctl_el2
    orr     x0, x0, #0x400          /* CNTHCTL.EL1PCEN */
    orr     x0, x0, #0x800          /* CNTHCTL.EL1PCTEN */
    msr     cnthctl_el2, x0

    /* disable EL1 FPU traps (CPACR.FPEN) */
    mov     x0, #(0b11 << 20)
    msr     cpacr_el1, x0

    /* keep the entry SP available at EL1 */
    mov     x0, sp
    msr     sp_el1, x0

    /* drop any EL1 TLB/I-cache state left by the previous world */
    tlbi    alle1is
    ic      iallu
    dsb     sy
    isb
    eret

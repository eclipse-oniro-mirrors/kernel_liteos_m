/*
 * RK3568 AMP boot entry (arm64_entry). Entered at EL1 (PSCI CPU_ON from
 * Linux/MICA via TF-A) or EL2/EL3 (QEMU -kernel / direct load).
 *
 * Flow (plan section 2.1):
 *   read CurrentEL -> arm64_elX_to_el1() -> SP_EL1 -> MMU identity map on
 *   -> zero .bss -> VBAR_EL1 -> main()
 */

    .arch armv8-a

    .extern arm64_elX_to_el1
    .extern arm64_exception_handlers
    .extern main
    .extern __bss_start
    .extern __bss_end
    .extern __el1_stack_top

    .section .text.entry, "ax"
    .align  4
    .global arm64_entry
    .type   arm64_entry, %function
arm64_entry:
    /* interrupts/faults off until we are ready */
    msr     daifset, #0xf

    /* ---- normalize to EL1h (SP_EL1) ---- */
    mrs     x1, CurrentEL
    cmp     x1, #(0b01 << 2)
    b.eq    1f
    bl      arm64_elX_to_el1     /* returns here at EL1h */
1:
    msr     spsel, #1            /* make sure we run on SP_EL1 */

    /* ---- build the boot stack ---- */
    ldr     x0, =__el1_stack_top
    mov     sp, x0

    /* ---- take over the translation state: the PSCI/OP-TEE NS handoff
     *      leaves whatever SCTLR_EL1 the previous world had. Write a
     *      canonical value (I-cache only: M=0 C=0, no WXN/A/E0E) and
     *      drop every stale translation BEFORE touching RAM. ---- */
    ldr     x0, =0x1000         /* I only */
    msr     sctlr_el1, x0
    isb
    tlbi    vmalle1is
    ic      iallu
    dsb     sy
    isb

    /* ---- exception vectors FIRST: from here on any fault (MMU bring-up
     *      included) lands in our own handler instead of a stale VBAR ---- */
    ldr     x0, =arm64_exception_handlers
    msr     vbar_el1, x0
    isb

    /* ---- .bss clear first: page tables live in .bss and must be zero
     *      before MMU init (32 bytes per iteration, MMU still off) ---- */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
2:
    cmp     x0, x1
    b.hs    3f
    stp     xzr, xzr, [x0], #16
    stp     xzr, xzr, [x0], #16
    b       2b
3:

    /* ---- MMU identity map (board static page tables) ---- */
    bl      LosMmuInit

    /* ---- percpu / current-task slot ---- */
    msr     tpidr_el1, xzr

    /* ---- into the C world ---- */
    bl      main

hang:
    msr     daifset, #0xf
    wfe
    b       hang

    .size arm64_entry, . - arm64_entry

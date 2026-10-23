/*
 * RK3568 AMP liteos_m linker script (AArch64, ELF loaded by the
 * OpenHarmony mcs_km loader / mica_test into the mcs reserved window).
 *
 * Memory contract with the Linux side (must match mcs_km.ko and the
 * master DTS oh_rt reserved-memory node, see kernel/liteos_m
 * components/mcs/README.md):
 *   - image base  : 0x74100000 (inside the 64MB mcs reserved window
 *                   0x74000000..0x77FFFFFF declared in the master DTS;
 *                   the window head 0x74000000..0x740FFFFF holds the
 *                   master-allocated openamp vrings, do not place code
 *                   or static data there)
 *   - region size : 63MB (0x74100000 .. 0x78000000)
 *   - entry       : arm64_entry at image offset 0 (PSCI CPU_ON entry)
 *   - .resource_table must stay a loaded section: the master-side
 *                   remoteproc loader parses it from the ELF and writes
 *                   the allocated vring device addresses back into it.
 */

ENTRY(arm64_entry)

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

MEMORY
{
    RTOS_RAM (rwx) : ORIGIN = 0x74100000, LENGTH = 63M
}

SECTIONS
{
    . = ORIGIN(RTOS_RAM);

    /* PSCI CPU_ON entry point: image offset 0 */
    .text.entry : {
        __text_entry_start = .;
        KEEP(*(.text.entry))
        __text_entry_end = .;
    } > RTOS_RAM

    /* VBAR_EL1 vector table: must be 2KB aligned. */
    .vectors : ALIGN(0x800) {
        __vectors_start = .;
        KEEP(*(.vectors))
        . = ALIGN(0x800);
        __vectors_end = .;
    } > RTOS_RAM

    .sysinitcall : ALIGN(0x1000) {
        __sysinitcall0_start = .;
        KEEP(*(SORT(.sysinitcall_level*)))
        __sysinitcall_end = .;
        __sysinitcall1_start = .;
        __sysinitcall2_start = .;
        __sysinitcall3_start = .;
        __sysinitcall4_start = .;
        __sysinitcall5_start = .;
        __sysinitcall6_start = .;
        __sysinitcall7_start = .;
        /* g_hwiForm (.vector) is filled at run time by ArchInit: keep it in
         * this page-aligned RW block ahead of .text so it never shares the
         * RO entry page (0x74100000..0x74101000). Needed since the merged
         * los_exc.S stopped emitting .vectors, which used to push this block
         * past 0x74101000 on its own. */
        *(.vector)
    } > RTOS_RAM
    __sysinitcall_start = __sysinitcall0_start;

    /* ---- .text ---- */
    .text : ALIGN(0x1000) {
        __text_start = .;
        *(.text* .gnu.linkonce.t.*)
        __text_end = .;
    } > RTOS_RAM

    /* ---- .rodata ---- */
    .rodata : ALIGN(0x1000) {
        __rodata_start = .;
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        __exc_table_start = .;
        KEEP(*(__exc_table))
        __exc_table_end = .;
        __rodata_end = .;
    } > RTOS_RAM

    .data : ALIGN(0x8) {
        __ram_data_start = .;
        *(.data .data.* .gnu.linkonce.d.*)
        . = ALIGN(0x8);
        KEEP(*( SORT (.liteos.table.*)));
        __ram_data_end = .;
    } > RTOS_RAM

    /*
     * MICA/remoteproc resource table: page aligned at the tail of the
     * data region, kept, so the Linux side can locate and map it
     * independently of other sections.
     */
    .resource_table : ALIGN(0x8) {
        __resource_table_start = .;
        KEEP(*(.resource_table))
        . = ALIGN(0x8);
        __resource_table_end = .;
    } > RTOS_RAM

    .bss (NOLOAD) : ALIGN(0x8) {
        __bss_start = .;
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(0x8);
        __bss_end = .;
    } > RTOS_RAM

    /* ---- EL1 mode stacks ---- */
    .stack (NOLOAD) : ALIGN(0x10) {
        __el1_stack = .;
        . += 0x8000;
        __el1_stack_top = .;
        __irq_stack = .;
        . += 0x4000;
        __irq_stack_top = .;
    } > RTOS_RAM

    . = ALIGN(0x1000);
    __heap_start = .;
    . = . + 0x00400000;      /* 4MB heap */
    __heap_end = .;
    __heap_size = 0x00400000;

    . = ALIGN(0x4000);
    _end = .;

    /DISCARD/ : {
        *(.comment .note .note.*)
        *(.eh_frame .eh_frame.*)
        *(.ARM.attributes)
        *(.ARM.exidx* .ARM.extab* .gnu.linkonce.armexidx.* .gnu.linkonce.armextab.*)
    }
}

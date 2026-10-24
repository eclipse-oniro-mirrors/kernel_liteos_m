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
 *   - region size : 61MB (0x74100000 .. 0x77DFFFFF); the tail hosts
 *                   two board-static windows NOT owned by the linker
 *                   heap: net DMA pool 0x77A00000 + 2M (gmac1) and
 *                   boot-probe scratch 0x77C00000 + 2M, so the heap
 *                   must end before 0x77A00000 (ASSERT below)
 *   - entry       : reset_vector at image offset 0 (PSCI CPU_ON entry)
 *   - .resource_table must stay a loaded section: the master-side
 *                   remoteproc loader parses it from the ELF and writes
 *                   the allocated vring device addresses back into it.
 */

ENTRY(reset_vector)

OUTPUT_FORMAT("elf64-littleaarch64", "elf64-littleaarch64", "elf64-littleaarch64")
OUTPUT_ARCH(aarch64)

MEMORY
{
    RTOS_RAM (rwx) : ORIGIN = 0x74100000, LENGTH = 61M
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

    /* Init tables + hwi form block: run-time data (ArchInit fills the
     * g_hwiForm interrupt table). Kept page-aligned in one block between
     * the entry page and .text so the dynamic MMU regions can map it
     * RW+NX while code stays RO+X. */
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
    } > RTOS_RAM
    __sysinitcall_start = __sysinitcall0_start;

    /* hwi form (LITE_OS_SEC_VEC, written by ArchInit) + inert compiler
     * init arrays: explicit placement, same RW block as sysinitcall. */
    .hwi_vector : ALIGN(0x40) {
        __hwi_vector_start = .;
        KEEP(*(.vector))
        __hwi_vector_end = .;
    } > RTOS_RAM

    .init_array : ALIGN(0x40) {
        KEEP(*(.init_array*))
    } > RTOS_RAM

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

/* heap must never run into the board-static net DMA pool at 0x77A00000 */
ASSERT(__heap_end <= (0x77A00000), "heap overruns the net DMA pool window")

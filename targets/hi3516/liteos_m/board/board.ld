/* Cortex-A7 linker script (Hi3516DV300, identity mapping 0x80000000). */

ENTRY(reset_vector)

OUTPUT_FORMAT("elf32-littlearm", "elf32-littlearm", "elf32-littlearm")
OUTPUT_ARCH(arm)

KERNEL_BASE = 0x80000000;          /* DDR base (identity mapped) */
KERNEL_SIZE = 0x04000000;          /* 64MB reserved */

MEMORY
{
    ram : ORIGIN = 0x80000000, LENGTH = KERNEL_SIZE
}

/* SMP per-core sortlink/stack multiplier */
CORE_NUM = 2;

SECTIONS
{
    . = KERNEL_BASE;

    .vectors : {
        *cortex-a7/gcc/los_exc.o(.vectors)
        *cortex-a7/gcc/los_exc.o(.vectors.*)
        KEEP(*(.vectors))
    } > ram

    .sysinitcall : {
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
    } > ram
    __sysinitcall_start = __sysinitcall0_start;

    /* ---- .text ---- */
    .text ALIGN(0x1000) : {
        __text_start = .;
        *(.text* .gnu.linkonce.t.*)
        __text_end = .;
    } > ram

    /* ---- .rodata ---- */
    .rodata ALIGN(0x1000) : {
        __rodata_start = .;
        *(.rodata .rodata.* .gnu.linkonce.r.*)
        __exc_table_start = .;
        KEEP(*(__exc_table))
        __exc_table_end = .;
        __rodata_end = .;
    } > ram

    .data ALIGN(0x1000) : {
        __ram_data_start = .;
        *(.data .data.* .gnu.linkonce.d.*)
        . = ALIGN(0x4);
        KEEP(*( SORT (.liteos.table.*)));
        __ram_data_end = .;
    } > ram

    .bss ALIGN(0x1000) (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* .gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(0x8);
        __bss_end = .;
    } > ram

    /* mode stacks (per-core, full descending) */
    .stack ALIGN(0x1000) (NOLOAD) : {
        __svc_stack = .;        . += 0x4000 * CORE_NUM;    __svc_stack_top = .;
        __exc_stack = .;        . += 0x1000 * CORE_NUM;    __exc_stack_top = .;
        __irq_stack = .;        . += 0x400 * CORE_NUM;     __irq_stack_top = .;
        __fiq_stack = .;        . += 0x400 * CORE_NUM;     __fiq_stack_top = .;
        __abt_stack = .;        . += 0x400 * CORE_NUM;     __abt_stack_top = .;
        __und_stack = .;        . += 0x400 * CORE_NUM;     __und_stack_top = .;
    } > ram

    . = ALIGN(0x1000);
    __heap_start = .;
    . = . + 0x00800000;
    __heap_end = .;
    __heap_size = 0x00800000;

    . = ALIGN(0x4000);
    _end = .;

    /DISCARD/ : {
        *(.comment .note .note.*)
        *(.eh_frame .eh_frame.*)
        *(.ARM.attributes)
        *(.ARM.exidx* .ARM.extab* .gnu.linkonce.armexidx.* .gnu.linkonce.armextab.*)
    }
}

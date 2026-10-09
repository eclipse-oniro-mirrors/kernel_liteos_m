/*
 * Copyright (c) 2026 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: HI3322 linker script for liteos_m (real Hi3322 memory map)
 *
 * Boot flow: SSB/seliteos copies the first 0x800 bytes of the app image
 * (the vector/reset_vector region, after the 0x300 sign header) from flash
 * 0x260C0300 into ITCM 0x10000 and jumps there. The rest of the code is
 * executed in place (XIP) from flash 0x260C0B00+. Data/bss/stack/heap live
 * in DTCM 0x10010000.
 *
 * Memory layout (Hi3322):
 *   BOOTUP_RAM    : 0x00010000, 0x00000800   (ITCM, reset_vector + vectors, VMA)
 *   BOOTUP_LOAD   : 0x260C0300, 0x00000800   (flash, LMA of the vector region)
 *   FLASH_PROGRAM : 0x260C0B00, 0x0087F500   (flash, XIP code/rodata/data-load)
 *   ITCM          : 0x00010800, 0x0003F800   (ITCM fast code, optional)
 *   DTCM          : 0x10010000, 0x00090000   (data/bss/stack/heap)
 */

OUTPUT_ARCH("riscv")
ENTRY(reset_vector)

MEMORY
{
    BOOTUP_RAM     (rx) : ORIGIN = 0x00010000, LENGTH = 0x00000800
    BOOTUP_LOAD    (rx) : ORIGIN = 0x260C0300, LENGTH = 0x00000800
    FLASH_PROGRAM  (rx) : ORIGIN = 0x260C0B00, LENGTH = 0x0087F500
    ITCM           (rx) : ORIGIN = 0x00010800, LENGTH = 0x0003F800
    DTCM           (rw) : ORIGIN = 0x10010000, LENGTH = 0x00090000
}

__STACK_SIZE = 0x2000;

/* PMP range for the OHOS data region (DTCM). pmp_init() uses these. */
__sram_start__ = ORIGIN(DTCM);
__sram_end__   = ORIGIN(DTCM) + LENGTH(DTCM);

SECTIONS
{
    /*
     * reset_vector + vectors: copied to ITCM 0x10000 by SSB. Must stay first
     * in the image (LMA 0x260C0300) and fit in 0x800 bytes.
     */
    .startup : ALIGN(8) {
        KEEP(*(.start.text))
        KEEP(*(.text.entry))
        KEEP(*(.text.entry2))
        . = ALIGN(8);
    } > BOOTUP_RAM AT > BOOTUP_LOAD

    /* Main code/rodata: XIP from flash 0x260C0B00. */
    .text : ALIGN(8) {
        KEEP(*(.interrupt.text))
        KEEP(*(.interrupt.HalTrapVector.text))
        KEEP(*(.interrupt.*))
        *(.text)
        *(.text.*)
        *(.itcm.text)
        *(.itcm.text.*)
        *(.rodata)
        *(.rodata.*)
        . = ALIGN(8);
    } > FLASH_PROGRAM

    /*
     * ITCM-resident code (SFC flash write/erase): while the SFC executes
     * a command the XIP window cannot fetch, so that path must run from
     * ITCM.
     */
    .itcm_resident : ALIGN(8) {
        __itcm_load_start = LOADADDR(.itcm_resident);
        __itcm_start = .;
        KEEP(*(.nor.itcm.text))
        KEEP(*(.nor.itcm.text.*))
        . = ALIGN(8);
        __itcm_end = .;
    } > ITCM AT > FLASH_PROGRAM

    .data : ALIGN(8) {
        __data_load_start = LOADADDR(.data);
        __data_start = .;
        *(.data)
        *(.data.*)
        *(.sdata)
        *(.sdata.*)
        *(.got)
        *(.got.plt)
        *(.got.*)
        . = ALIGN(8);
        __data_end = .;
    } > DTCM AT > FLASH_PROGRAM

    .bss (NOLOAD) : ALIGN(8) {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(.sbss)
        *(.sbss.*)
        *(.scommon)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    } > DTCM

    .stack (NOLOAD) : ALIGN(8) {
        . = . + __STACK_SIZE;
        __stack_top__ = .;
        __start_and_irq_stack_top = .;
    } > DTCM

    .exc_stack (NOLOAD) : ALIGN(8) {
        . = . + 0x400;
        __exc_stack_top = .;
        __except_stack_top = .;
    } > DTCM

    .nmi_stack (NOLOAD) : ALIGN(8) {
        . = . + 0x400;
        __nmi_stack_top = .;
    } > DTCM

    .irq_stack (NOLOAD) : ALIGN(8) {
        __int_stack_start = .;
        . = . + 0x800;
        __int_stack_end = .;
        __irq_stack_top = .;
    } > DTCM

    .heap (NOLOAD) : ALIGN(8) {
        __heap_begin__ = .;
        __heap_start = .;
        g_intheap_begin = ABSOLUTE(.);
        g_intheap_size = (ORIGIN(DTCM) + LENGTH(DTCM)) - g_intheap_begin;
        . = ORIGIN(DTCM) + LENGTH(DTCM);
        __heap_end__ = .;
    } > DTCM

    _gp_ = . + 0x800;
    __global_pointer$ = _gp_;

    .initcall : ALIGN(8) {
        __zinitcall_bsp_start = .;
        KEEP(*(.zinitcall.bsp.*))
        __zinitcall_bsp_end = .;
        . = ALIGN(4);
        __zinitcall_device_start = .;
        KEEP(*(.zinitcall.device.*))
        __zinitcall_device_end = .;
        . = ALIGN(4);
        __zinitcall_core_start = .;
        KEEP(*(.zinitcall.core.*))
        __zinitcall_core_end = .;
        . = ALIGN(4);
        __zinitcall_sys_service_start = .;
        KEEP(*(.zinitcall.sys.service.*))
        __zinitcall_sys_service_end = .;
        . = ALIGN(4);
        __zinitcall_sys_feature_start = .;
        KEEP(*(.zinitcall.sys.feature.*))
        __zinitcall_sys_feature_end = .;
        . = ALIGN(4);
        __zinitcall_run_start = .;
        KEEP(*(.zinitcall.run.*))
        __zinitcall_run_end = .;
        . = ALIGN(4);
        __zinitcall_app_service_start = .;
        KEEP(*(.zinitcall.app.service.*))
        __zinitcall_app_service_end = .;
        . = ALIGN(4);
        __zinitcall_app_feature_start = .;
        KEEP(*(.zinitcall.app.feature.*))
        __zinitcall_app_feature_end = .;
        . = ALIGN(4);
    } > FLASH_PROGRAM

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}

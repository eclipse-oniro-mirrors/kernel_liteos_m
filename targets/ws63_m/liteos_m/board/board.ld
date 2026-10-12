/*
 * Copyright (c) 2024 HiSilicon (Shanghai) Technologies Co., Ltd.
 * Description: WS63 linker script for liteos_m
 *
 * Memory layout matches bootloader (from 2.08.5 board.ld.S):
 *   ROM             : 0x109000,  0x43000
 *   ITCM            : 0x14C000,  0x4000
 *   DTCM            : 0x180000,  0x4000
 *   SRAM            : 0xA00000,  0x85F00
 *   PROGRAM_STARTUP : 0x230300,  0x1FF000
 *
 * Reverted to SDK original layout:
 *   - _gp_ (not __global_pointer$) to avoid GCC gp-relative relaxation
 *   - g_intheap_begin/g_intheap_size with ABSOLUTE() for correct heap size
 *   - 4 separate stack sections (main/exc/nmi/irq) matching SDK
 */

OUTPUT_ARCH("riscv")
ENTRY(reset_vector)

MEMORY
{
    ROM             : ORIGIN = 0x109000,   LENGTH = 0x43000
    ITCM            : ORIGIN = 0x14C000,   LENGTH = 0x4000
    DTCM            : ORIGIN = 0x180000,   LENGTH = 0x4000
    SRAM            : ORIGIN = 0xA00000,   LENGTH = 0x85F00
    PROGRAM_STARTUP : ORIGIN = 0x230300,   LENGTH = 0x1FF000
}

__STACK_SIZE = 0x2000;

/* PMP entry 5 边界符号：覆盖 OHOS 实际使用的 SRAM 范围
 * 见 device/board/hihope/ws63_m/liteos_m/board/pmp_init.c
 */
__sram_start__ = ORIGIN(SRAM);
__sram_end__   = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
    .startup : ALIGN(8) {
        KEEP(*(.start.text))
        KEEP(*(.text.entry))
        KEEP(*(.text.entry2))
        *(.interrupt.text)
        *(.interrupt.HalTrapVector.text)
        *(.interrupt.*)
        *(.text)
        *(.text.*)
        *(.itcm.text)
        *(.itcm.text.*)
        *(.rodata)
        *(.rodata.*)
    } > PROGRAM_STARTUP

    /* .init_array / .fini_array: function-pointer tables emitted by GCC for
     * C++ static ctors/dtors and (when LOSCFG_TEST_KERNEL_COVERAGE=y) the
     * gcov per-TU constructors that register gcov_info. Read-only, placed
     * in flash alongside .startup; TEST_GcovRunConstructors() iterates
     * .init_array.
     *
     * GNU ld scripts have no conditional compilation, so these sections
     * cannot be macro-gated. Without instrumentation they are empty (zero
     * size, no section headers in the ELF, __init_array_start == end) and
     * byte-identical output has been verified against a pristine build;
     * test_gcov.c also declares the bound symbols weak with a NULL guard,
     * so linking stays safe on boards whose script lacks these sections. */
    .init_array : ALIGN(4) {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
        __init_array_end = .;
    } > PROGRAM_STARTUP

    .fini_array : ALIGN(4) {
        __fini_array_start = .;
        KEEP(*(.fini_array))
        KEEP(*(.fini_array.*))
        __fini_array_end = .;
    } > PROGRAM_STARTUP

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
    } > SRAM AT > PROGRAM_STARTUP

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
    } > SRAM

    .stack (NOLOAD) : ALIGN(8) {
        . = . + 0x400;
        __exc_stack_top = .;
        __except_stack_top = .;
        . = . + __STACK_SIZE;
        __stack_top__ = .;
        __start_and_irq_stack_top = .;
    } > SRAM

    .nmi_stack (NOLOAD) : ALIGN(8) {
        . = . + 0x400;
        __nmi_stack_top = .;
    } > SRAM

    .irq_stack (NOLOAD) : ALIGN(8) {
        __int_stack_start = .;
        . = . + 0x800;
        __int_stack_end = .;
        __irq_stack_top = .;
    } > SRAM

    .heap (NOLOAD) : ALIGN(8) {
        __heap_begin__ = .;
        __heap_start = .;
        g_intheap_begin = ABSOLUTE(.);
        g_intheap_size = (ORIGIN(SRAM) + LENGTH(SRAM)) - g_intheap_begin;
        . = ORIGIN(SRAM) + LENGTH(SRAM);
        __heap_end__ = .;
    } > SRAM

    _gp_ = . + 0x800;

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
    } > PROGRAM_STARTUP

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}

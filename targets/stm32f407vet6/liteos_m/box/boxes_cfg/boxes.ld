/*
 * Box sections for the two demo boxes, passed to the link as a SECOND -T
 * script from board/BUILD.gn (this binutils resolves INCLUDE only via the
 * current working directory, so a plain INCLUDE inside liteos.ld fails).
 *
 * Slot geometry (matches liteos.ld MEMORY and include/asm/memmap_config.h):
 *   box A: code 0x08040000 / 64K (64K-aligned) -> one MPU region (R3)
 *          data 0x20010000 / 32K (32K-aligned) -> one MPU region (R4)
 *   box B: code 0x08050000 / 64K
 *          data 0x20018000 / 32K
 *
 * The RAM slot holds .BOX_x_DATA + .BOX_x_BSS + .BOX_x_HEAP and always ends
 * at the slot boundary, so one 32K MPU region covers the whole slot whatever
 * the content mix; the heap claims whatever is left after data+bss.
 *
 * Selectors match every object whose file name contains "boxA"/"boxB"
 * (usr_demo/tboxA compiles to boxA_main.o; a libtboxA.o would match too).
 * Framework objects such as boxes_conf.o match neither and stay in kernel
 * sections. NOTE: box objects must reach the link line as individual .o
 * files (GN source_set); archive members are pulled on symbol demand and
 * these selectors may miss them inside static_library archives.
 *
 * Symbol names prefixed with __boxA_ and __boxB_ are the contract of
 * boxes_cfg/boxes_conf.h, keep them unchanged.
 */

SECTIONS
{
.BOX_A_TEXT : {
    __boxA_text_start = .;
    KEEP(*boxA*.o(.text* .text.*))
    __boxA_rodata_start = .;
    KEEP(*boxA*.o(.rodata* .rodata.*))
    . = ALIGN(0x4);
    __boxA_rodata_end = .;
    __boxA_text_end = .;
} > BOX_A_FLASH

ASSERT((__boxA_text_end - __boxA_text_start) <= LENGTH(BOX_A_FLASH),
       "boxA text+rodata exceeds its 64K flash slot")

.BOX_A_DATA : {
    __boxA_data_start = .;
    KEEP(*boxA*.o(.data* .data.*))
    KEEP(*boxA*.o(.boxA.data* .boxA.res*))
    . = ALIGN(0x4);
} > BOX_A_RAM AT> KERNEL_FLASH
__boxA_data_load_start = LOADADDR(.BOX_A_DATA);
__boxA_data_load_size = SIZEOF(.BOX_A_DATA);
__boxA_data_load_end = __boxA_data_load_start + __boxA_data_load_size;

.BOX_A_BSS (NOLOAD) : {
    __boxA_bss_start = .;
    /* Only .boxA.bss* here: a widened .boxA.* would silently drop
     * initialized .boxA.data content into this NOLOAD section. */
    KEEP(*boxA*.o(.bss* .bss.* COMMON .boxA.bss*))
    . = ALIGN(0x4);
    __boxA_bss_end = .;
} > BOX_A_RAM
__boxA_bss_size = __boxA_bss_end - __boxA_bss_start;

.BOX_A_HEAP (NOLOAD) : ALIGN(0x4) {
    __boxA_heap_start = .;
    . = ORIGIN(BOX_A_RAM) + LENGTH(BOX_A_RAM);
    __boxA_heap_end = .;
    __boxA_data_end = .;
} > BOX_A_RAM
__boxA_heap_size = __boxA_heap_end - __boxA_heap_start;
__boxA_data_sec_size = __boxA_data_end - __boxA_data_start;

ASSERT((__boxA_data_sec_size <= LENGTH(BOX_A_RAM)),
       "boxA data+bss+heap exceeds its 32K RAM slot")

.BOX_B_TEXT : {
    __boxB_text_start = .;
    KEEP(*boxB*.o(.text* .text.*))
    __boxB_rodata_start = .;
    KEEP(*boxB*.o(.rodata* .rodata.*))
    . = ALIGN(0x4);
    __boxB_rodata_end = .;
    __boxB_text_end = .;
} > BOX_B_FLASH

ASSERT((__boxB_text_end - __boxB_text_start) <= LENGTH(BOX_B_FLASH),
       "boxB text+rodata exceeds its 64K flash slot")

.BOX_B_DATA : {
    __boxB_data_start = .;
    KEEP(*boxB*.o(.data* .data.*))
    KEEP(*boxB*.o(.boxB.data* .boxB.res*))
    . = ALIGN(0x4);
} > BOX_B_RAM AT> KERNEL_FLASH
__boxB_data_load_start = LOADADDR(.BOX_B_DATA);
__boxB_data_load_size = SIZEOF(.BOX_B_DATA);
__boxB_data_load_end = __boxB_data_load_start + __boxB_data_load_size;

.BOX_B_BSS (NOLOAD) : {
    __boxB_bss_start = .;
    KEEP(*boxB*.o(.bss* .bss.* COMMON .boxB.bss*))
    . = ALIGN(0x4);
    __boxB_bss_end = .;
} > BOX_B_RAM
__boxB_bss_size = __boxB_bss_end - __boxB_bss_start;

.BOX_B_HEAP (NOLOAD) : ALIGN(0x4) {
    __boxB_heap_start = .;
    . = ORIGIN(BOX_B_RAM) + LENGTH(BOX_B_RAM);
    __boxB_heap_end = .;
    __boxB_data_end = .;
} > BOX_B_RAM
__boxB_heap_size = __boxB_heap_end - __boxB_heap_start;
__boxB_data_sec_size = __boxB_data_end - __boxB_data_start;

ASSERT((__boxB_data_sec_size <= LENGTH(BOX_B_RAM)),
       "boxB data+bss+heap exceeds its 32K RAM slot")
}

/*
 * STM32F407VET6 kernel/user MPU isolation layout.
 *
 * Section names: UPPERCASE_WITH_UNDERSCORES (custom sections only;
 * standard ELF section names .text/.data/.bss/.got/.ARM.exidx stay as is).
 *
 *  FLASH 1MB @0x08000000 (die measured 1MB via FSIZE; part number 512KB):
 *    0x08000000  kernel code + load images    128K  no MPU region: kernel
 *    0x08020000  common code (R0)            128K  reaches via PRIVDEFENA
 *    0x08040000  box A code slot (R3)         64K  background, user access
 *    0x08050000  box B code slot (R3)          64K  -> MemManage fault
 *    0x08060000  spare                        640K
 *  SRAM 128KB @0x20000000:
 *    0x20000000  kernel RAM                    32K  no MPU region
 *    0x20008000  common data (R1)              16K  AP=001 RW/RW
 *    0x2000C000  user read-only (R2)           16K  AP=010 kernel RW/user RO
 *    0x20010000  box A data slot (R4)          32K  fixed slot, one region
 *    0x20018000  box B data slot (R4)          32K  fixed slot, one region
 *  CCM 64KB @0x10000000: kernel only (core coupled, DMA cannot reach it).
 *
 * Adapted from arm_mps2_an386/liteos_m/board/liteos.ld:
 * - code/rodata stay in flash, .data keeps its LMA in flash (startup.s copy)
 * - .init_array/.fini_array/.sysinitcall are merged into .data
 * - kernel heap is bounded by the kernel RAM region (0x20008000)
 * - box sections come from box/boxes_cfg/boxes.ld (INCLUDE below)
 */

OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(Vector_Table)

SVC_IRQ_STACK_SIZE  = 4K;

MEMORY
{
   KERNEL_FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 128K
   COMMON_FLASH (rx)      : ORIGIN = 0x08020000, LENGTH = 128K
   BOX_A_FLASH (rx)       : ORIGIN = 0x08040000, LENGTH = 64K
   BOX_B_FLASH (rx)       : ORIGIN = 0x08050000, LENGTH = 64K
   KERNEL_RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 32K
   COMMON_RAM (rw)        : ORIGIN = 0x20008000, LENGTH = 16K
   USER_READONLY_RAM (rw) : ORIGIN = 0x2000C000, LENGTH = 16K
   BOX_A_RAM (rw)         : ORIGIN = 0x20010000, LENGTH = 32K
   BOX_B_RAM (rw)         : ORIGIN = 0x20018000, LENGTH = 32K
   CCM (rw)               : ORIGIN = 0x10000000, LENGTH = 64K
}

SECTIONS
{
  /* Boot vector table (16 system entries from startup.s), stays in flash. */
  .BOOT_VECTOR_TABLE : {
      __boot_vector_start = .;
      KEEP (*(SORT_NONE(.BOOT_VECTOR_TABLE)))
      __boot_vector_end = .;
  } > KERNEL_FLASH

  /* Kernel runtime vector table (g_hwiForm, input section ".vector" from
   * LITE_OS_SEC_VEC): must be WRITABLE RAM — HalHwiInit populates every entry
   * and points VTOR here during LOS_KernelInit. NOLOAD is safe because all
   * entries are rewritten at init; 512-byte alignment satisfies VTOR
   * granularity for 144 vectors. */
  .KERNEL_VECTOR_TABLE (NOLOAD) : ALIGN(0x200) {
      . = ALIGN(0x200);
      *(.vector)
  } > KERNEL_RAM

  .got : {
    *(.got.plt)
    *(.igot.plt)
    *(.got)
    *(.igot)
  } > KERNEL_FLASH

  .text : {
    . = ALIGN(0x4);
    __text_start = .;
    _stext = .;
    /* Box objects fall through to their slots in boxes.ld. Plain .text
     * (arch asm: dispatch/exc/startup) must be collected here too,
     * otherwise ld places it as an uncontrolled orphan section.
     *
     * NOTE: a single pattern list must not mix a literal name with
     * wildcards - this binutils then skips the EXCLUDE_FILE check for
     * wildcard matches (box .text.boxAEntry leaked into the kernel).
     * Keep literals and wildcards in separate statements.
     * libc mem/str primitives are EXCLUDED from the wildcard so their
     * .text.* sections fall through to the .COMMON_TEXT rules: the box
     * world shares the libc primitive surface (memcpy/memset/strlen/...)
     * and a kernel-flash landing is a user-mode MemManage fault; kernel
     * callers cross-call the COMMON copy (R0 is kernel-executable).
     * mem family + libsec mem{set,cpy}_s: wildcard (all members are pure
     * helpers); str family: 16 explicit members (strdup/strtok/strtol/
     * strerror/strftime/strcase* stay kernel-only - rodata tables,
     * malloc or locale deps). */
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *libmusl-c.mem*.o *libsec.mem*.o *libmusl-c.strcat.o *libmusl-c.strchr.o *libmusl-c.strchrnul.o *libmusl-c.strcmp.o *libmusl-c.strcpy.o *libmusl-c.strcspn.o *libmusl-c.strncat.o *libmusl-c.strncmp.o *libmusl-c.strncpy.o *libmusl-c.strnlen.o *libmusl-c.strrchr.o *libmusl-c.strspn.o *libmusl-c.strstr.o *libmusl-c.strlen*.o) .text.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *libmusl-c.mem*.o *libsec.mem*.o *libmusl-c.strcat.o *libmusl-c.strchr.o *libmusl-c.strchrnul.o *libmusl-c.strcmp.o *libmusl-c.strcpy.o *libmusl-c.strcspn.o *libmusl-c.strncat.o *libmusl-c.strncmp.o *libmusl-c.strncpy.o *libmusl-c.strnlen.o *libmusl-c.strrchr.o *libmusl-c.strspn.o *libmusl-c.strstr.o *libmusl-c.strlen*.o) .text)
    /* board/include/los_builddef_target.h routes kernel code via
     * LITE_OS_SEC_TEXT* to .kernel.text; ITCM variant falls back to
     * normal flash (STM32F407 has no ITCM). */
    *(.kernel.text .kernel.text.*)
    *(.itcm.kernel.text)
    . = ALIGN(0x4);
    _etext = .;
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .rodata.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .gnu.linkonce.r.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .rodata)
    *(.rodata1)
    . = ALIGN(0x4);
    __kernel_text_end = .;
  } > KERNEL_FLASH

  /* .ARM.exidx is sorted, so has to go in its own output section. */
  .ARM.exidx : ALIGN(0x8) {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > KERNEL_FLASH

  .data : ALIGN(0x4) {
    __data_start = .;
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .data.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .gnu.linkonce.d.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .data)
    *(.kernel.data .kernel.data.*)

    __init_array_start = .;
    KEEP(*(.init_array))
    KEEP(*(.init_array.*))
    __init_array_end = .;

    __fini_array_start = .;
    KEEP(*(.fini_array))
    KEEP(*(.fini_array.*))
    __fini_array_end = .;

    __sysinitcall_start = .;
    __sysinitcall0_start = .;
    KEEP(*(SORT(.sysinitcall_level0_*)))
    __sysinitcall1_start = .;
    KEEP(*(SORT(.sysinitcall_level1_*)))
    __sysinitcall2_start = .;
    KEEP(*(SORT(.sysinitcall_level2_*)))
    __sysinitcall3_start = .;
    KEEP(*(SORT(.sysinitcall_level3_*)))
    __sysinitcall4_start = .;
    KEEP(*(SORT(.sysinitcall_level4_*)))
    __sysinitcall5_start = .;
    KEEP(*(SORT(.sysinitcall_level5_*)))
    __sysinitcall6_start = .;
    KEEP(*(SORT(.sysinitcall_level6_*)))
    __sysinitcall7_start = .;
    KEEP(*(SORT(.sysinitcall_level7_*)))
    __sysinitcall_end = .;

    . = ALIGN(0x4);
    __data_end = .;
  } > KERNEL_RAM AT> KERNEL_FLASH
  __data_load = LOADADDR(.data);

  .bss : ALIGN(0x4) {
   __bss_start = .;
   *(.dynbss)
   *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .bss.*)
   *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .gnu.linkonce.b.*)
   *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o) .bss)
   *(.kernel.bss .kernel.bss.*)
   *(COMMON)
   . = ALIGN(0x4);
   __bss_end = .;
  } > KERNEL_RAM

  _sstack = .;
  .STACK : ALIGN(0x8) {
    _stack = .;
    __irq_stack_bootom = .;
    . += SVC_IRQ_STACK_SIZE;
    __irq_stack_top = .;
    _estack = .;
  } > KERNEL_RAM

  .HEAP (NOLOAD): ALIGN(0x40) {
    __heap_start = .;
  } > KERNEL_RAM

  /* Kernel heap is bounded by the kernel RAM region (0x20000000..0x20007FFF):
   * 0x20008000 onwards belongs to R1/R2/box slots. */
  __heap_size = ORIGIN(KERNEL_RAM) + LENGTH(KERNEL_RAM) - __heap_start;
  __heap_end = __heap_start + __heap_size;
  end = .;

  /* ---- MPU isolation sections (region numbers in asm/memmap_config.h) ---- */

  /* R0: common code, executable from kernel and user boxes. */
  .COMMON_TEXT : ALIGN(0x4) {
    __common_text_start = .;
    KEEP(*(.COMMON_TEXT .COMMON_TEXT.*))
    /* LITE_OS_SEC_TEXT_USRSPACE/USRROM: shared user code (uspace
     * entry, syscall trampolines written in asm). KEEP: an
     * unreferenced veneer must not fall to --gc-sections (a dropped
     * veneer = zeros in flash). */
    KEEP(*(.usr.text .usr.text.*))
    KEEP(*(.usrtcm.text))
    /* M5D single shared copy of the bestfit allocator (usr_demo:
     * box_usr_mem): route the WHOLE objects by file-name pattern —
     * explicit LITE_OS_SEC_* sections AND the plain .text.* / .rodata.*
     * of static/inline helpers, so no fragment of the shared allocator
     * (or its dlink-table / multipool helpers) stays behind in
     * kernel-only flash (executing there from a box would
     * MemManage-fault). KEEP: box calls it, --gc would keep it, but the
     * convention above is KEEP for shared code. */
    KEEP(*box_usr_mem.*(.text .text.* .rodata .rodata.*))
    /* libc mem/str primitives shared with the box world (M5E): the pure
     * helpers — mem family (musl mem*.o + libsec mem{set,cpy}_s) and 16
     * plain str members — carry no globals/rodata tables. The two
     * bare-.text members (memcpy_le.o, strlen-armv6t2.o: whole file is
     * one function; -ffunction-sections did not apply) need explicit
     * file rules; the rest arrive by function-section name. Members
     * left in kernel flash ON PURPOSE: strerror/strtol/strtod/strdup/
     * strtok/strftime/strptime (rodata tables, malloc or static state)
     * and the strcase* family (locale __ctype deps) — box code must not
     * reach them. */
    *libmusl-c.memcpy_le.o(.text)
    *libmusl-c.strlen-armv6t2.o(.text)
    *(.text.mem*)
    *(.text.strcat .text.strchr .text.__strchrnul .text.strcmp .text.strcpy
      .text.strcspn .text.strncat .text.strncmp .text.strncpy .text.strnlen
      .text.strrchr .text.strspn .text.strstr .text.twoway_strstr)
    __common_rodata_start = .;
    KEEP(*(.COMMON_RODATA .COMMON_RODATA.*))
    KEEP(*(.usr.ro.data))
    . = ALIGN(0x4);
    __common_rodata_end = .;
    __common_text_end = .;
  } > COMMON_FLASH

  /* R1: common data, RW from both kernel and user. */
  .COMMON_DATA : ALIGN(0x4) {
    __common_data_start = .;
    *(.COMMON_DATA .COMMON_DATA.*)
    /* M5D shared allocator INITIALIZED data would land here (explicit
     * .COMMON_DATA sections). NOTE: zero-initialized objects must NOT be
     * listed here — a NOBITS input section inside this PROGBITS/AT output
     * section shares its LMA slot with other NOBITS sections (ld does not
     * advance the AT cursor), so the startup copy loop would read
     * whatever real flash data occupies that address (caught by a
     * watchpoint: g_MALLOC_HOOK was seeded with string bytes). Zero
     * objects go to .COMMON_BSS below, zeroed by startup. */
    *box_usr_mem.*(.data .data.*)
    . = ALIGN(0x4);
    __common_data_end = .;
  } > COMMON_RAM AT> KERNEL_FLASH
  __common_data_load = LOADADDR(.COMMON_DATA);

  .COMMON_BSS (NOLOAD) : ALIGN(0x4) {
    __common_bss_start = .;
    *(.COMMON_BSS .COMMON_BSS.*)
    /* M5D shared allocator zero-initialized data: g_MALLOC_HOOK /
     * m_aucSysMem* / g_poolHead — zeroed by the startup common_bss loop
     * (COMMON_RAM is outside the __bss range). */
    *box_usr_mem.*(.bss .bss.* COMMON)
    . = ALIGN(0x4);
    __common_bss_end = .;
  } > COMMON_RAM

  /* R2: user read-only data, written by kernel only. */
  .USER_READONLY_DATA : ALIGN(0x4) {
    __user_readonly_data_start = .;
    *(.USER_READONLY_DATA .USER_READONLY_DATA.*)
    /* Box resource tables (.box.res): link-time initialized,
     * kernel-owned registration data. Boxes read them but must
     * not tamper, hence R2 (kernel RW / user RO). KEEP: scanned
     * at box init, unreferenced by code, gc-sections would drop. */
    KEEP(*(.box.res .box.res.*))
    . = ALIGN(0x4);
    __user_readonly_data_end = .;
  } > USER_READONLY_RAM AT> KERNEL_FLASH
  __user_readonly_data_load = LOADADDR(.USER_READONLY_DATA);

  .USER_READONLY_BSS (NOLOAD) : ALIGN(0x4) {
    __user_readonly_bss_start = .;
    *(.USER_READONLY_BSS .USER_READONLY_BSS.*)
    . = ALIGN(0x4);
    __user_readonly_bss_end = .;
  } > USER_READONLY_RAM

  /* CCM data RAM: fast CPU-only memory, DMA cannot reach it. NOLOAD
   * means no initializers are copied; runtime code must initialize
   * whatever it places here (startup.s copies initialized sections only). */
  .CCM (NOLOAD) : ALIGN(0x4) {
    __ccm_start = .;
    *(.CCM .CCM.*)
    . = ALIGN(0x4);
    __ccm_end = .;
  } > CCM

  /* Box A/B sections live in box/boxes_cfg/boxes.ld and enter the link as a
   * second -T script from board/BUILD.gn (this binutils resolves INCLUDE
   * only via the current working directory, so INCLUDE cannot be used). */

  /DISCARD/ : { *(.note.GNU-stack) *(.gnu_debuglink) *(.gnu.lto_*) }
}

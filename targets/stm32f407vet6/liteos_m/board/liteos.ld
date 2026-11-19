/*
 * STM32F407VET6 kernel/user MPU isolation layout.
 *
 * Section names: UPPERCASE_WITH_UNDERSCORES (custom sections only;
 * standard ELF section names .text/.data/.bss/.got/.ARM.exidx stay as is).
 *
 *  FLASH 1MB @0x08000000 (die measured 1MB via FSIZE; part number 512KB):
 *    0x08000000  kernel code + load images    256K  no MPU region: kernel
 *    0x08040000  common code (R0)            256K  reaches via PRIVDEFENA
 *    0x08080000  box A code slot (R3)         64K  background, user access
 *    0x08090000  box B code slot (R3)          64K  -> MemManage fault
 *    0x080A0000  spare                        384K
 *  SRAM 128KB @0x20000000:
 *    0x20000000  kernel RAM                    64K  no MPU region
 *    0x20010000  common data (R1)              16K  AP=001 RW/RW
 *    0x20014000  user read-only (R2)           16K  AP=010 kernel RW/user RO
 *    0x20018000  box A data slot (R4)          16K  fixed slot, one region
 *    0x2001C000  box B data slot (R4)          16K  fixed slot, one region
 *  CCM 64KB @0x10000000: kernel only (core coupled, DMA cannot reach it).
 *
 * R0 layout surgery (M6S, 2026-11-19): COMMON moved 0x08020000->0x08040000
 * and grew 128K->256K (256K-aligned MPU region, holds the shared code PLUS
 * the whole kernel-test suite set); KERNEL_FLASH absorbed the old COMMON
 * hole (128K->256K, kernel measured 58K - growth headroom); box slots
 * moved to 0x08080000/0x08090000, still 64K-aligned. All bases tracked in
 * include/asm/memmap_config.h (slot macros + RASR SIZE field); box layout
 * addresses derive from the __boxA/B_* link symbols, so boxes_cfg needs no
 * change. Requires the 1MB die (FSIZE-verified) - a 512KB part cannot
 * hold this layout (first out-of-bounds byte: 0x08080000).
 *
 * Adapted from arm_mps2_an386/liteos_m/board/liteos.ld:
 * - code/rodata stay in flash, .data keeps its LMA in flash (startup.s copy)
 * - .init_array/.fini_array/.sysinitcall are merged into .data
 * - kernel heap is bounded by the kernel RAM region (0x20010000)
 * - box sections come from box/boxes_cfg/boxes.ld (INCLUDE below)
 */

OUTPUT_FORMAT("elf32-littlearm", "elf32-bigarm", "elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(Vector_Table)

SVC_IRQ_STACK_SIZE  = 4K;

MEMORY
{
   KERNEL_FLASH (rx)      : ORIGIN = 0x08000000, LENGTH = 256K
   COMMON_FLASH (rx)      : ORIGIN = 0x08040000, LENGTH = 256K
   BOX_A_FLASH (rx)       : ORIGIN = 0x08080000, LENGTH = 64K
   BOX_B_FLASH (rx)       : ORIGIN = 0x08090000, LENGTH = 64K
   KERNEL_RAM (rwx)       : ORIGIN = 0x20000000, LENGTH = 64K
   COMMON_RAM (rw)        : ORIGIN = 0x20010000, LENGTH = 16K
   USER_READONLY_RAM (rw) : ORIGIN = 0x20014000, LENGTH = 16K
   BOX_A_RAM (rw)         : ORIGIN = 0x20018000, LENGTH = 16K
   BOX_B_RAM (rw)         : ORIGIN = 0x2001C000, LENGTH = 16K
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
     * malloc or locale deps).
     * libgcc float / 64-bit arithmetic members + musl frexp are excluded
     * too so they fall through to .COMMON_TEXT - see the comment there for
     * the member-by-member audit and for why the three unwinder members
     * are NOT in the list. */
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o *libmusl-c.mem*.o *libsec.mem*.o *libmusl-c.strcat.o *libmusl-c.strchr.o *libmusl-c.strchrnul.o *libmusl-c.strcmp.o *libmusl-c.strcpy.o *libmusl-c.strcspn.o *libmusl-c.strncat.o *libmusl-c.strncmp.o *libmusl-c.strncpy.o *libmusl-c.strnlen.o *libmusl-c.strrchr.o *libmusl-c.strspn.o *libmusl-c.strstr.o *libmusl-c.strlen*.o *_arm_addsubdf3.o *_arm_addsubsf3.o *_arm_cmpdf2.o *_arm_fixdfsi.o *_arm_fixunsdfsi.o *_arm_muldf3.o *_arm_muldivdf3.o *_arm_mulsf3.o *_arm_truncdfsf2.o *_aeabi_ldivmod.o *_aeabi_uldivmod.o *_dvmd_tls.o *_fixunsdfdi.o *_popcountsi2.o *_udivmoddi4.o *libmusl-m.frexp.o *libmusl-m.frexpl.o *It_los_queue*.o *LLt_los_queue*.o *Llt_los_queue*.o *It_los_event*.o) .text.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o *libmusl-c.mem*.o *libsec.mem*.o *libmusl-c.strcat.o *libmusl-c.strchr.o *libmusl-c.strchrnul.o *libmusl-c.strcmp.o *libmusl-c.strcpy.o *libmusl-c.strcspn.o *libmusl-c.strncat.o *libmusl-c.strncmp.o *libmusl-c.strncpy.o *libmusl-c.strnlen.o *libmusl-c.strrchr.o *libmusl-c.strspn.o *libmusl-c.strstr.o *libmusl-c.strlen*.o *_arm_addsubdf3.o *_arm_addsubsf3.o *_arm_cmpdf2.o *_arm_fixdfsi.o *_arm_fixunsdfsi.o *_arm_muldf3.o *_arm_muldivdf3.o *_arm_mulsf3.o *_arm_truncdfsf2.o *_aeabi_ldivmod.o *_aeabi_uldivmod.o *_dvmd_tls.o *_fixunsdfdi.o *_popcountsi2.o *_udivmoddi4.o *libmusl-m.frexp.o *libmusl-m.frexpl.o *It_los_queue*.o *LLt_los_queue*.o *Llt_los_queue*.o *It_los_event*.o) .text)
    /* board/include/los_builddef_target.h routes kernel code via
     * LITE_OS_SEC_TEXT* to .kernel.text; ITCM variant falls back to
     * normal flash (STM32F407 has no ITCM). */
    *(.kernel.text .kernel.text.*)
    *(.itcm.kernel.text)
    . = ALIGN(0x4);
    _etext = .;
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o *los_usr_init*.o *It_los_queue*.o *LLt_los_queue*.o *Llt_los_queue*.o *It_los_event*.o) .rodata.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o *los_usr_init*.o *It_los_queue*.o *LLt_los_queue*.o *Llt_los_queue*.o *It_los_event*.o) .gnu.linkonce.r.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o *los_usr_init*.o *It_los_queue*.o *LLt_los_queue*.o *Llt_los_queue*.o *It_los_event*.o) .rodata)
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
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o) .data.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o) .gnu.linkonce.d.*)
    *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o) .data)
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
   *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o) .bss.*)
   *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o) .gnu.linkonce.b.*)
   *(EXCLUDE_FILE(*boxA*.o *boxB*.o *box_usr_mem.*.o *box_usr_test.*.o *los_usr_swtmr_task*.o) .bss)
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
    /* The single shared copy of the bestfit allocator (usr_demo:
     * box_usr_mem): route the WHOLE objects by file-name pattern —
     * explicit LITE_OS_SEC_* sections AND the plain .text.* / .rodata.*
     * of static/inline helpers, so no fragment of the shared allocator
     * (or its dlink-table / multipool helpers) stays behind in
     * kernel-only flash (executing there from a box would
     * MemManage-fault). KEEP: box calls it, --gc would keep it, but the
     * convention above is KEEP for shared code. */
    KEEP(*box_usr_mem.*(.text .text.* .rodata .rodata.*))
    /* UTS single-copy test runner (usr_demo: box_usr_test): same
     * whole-object routing as the allocator above - the shared iCunit
     * (case table, module stats, fail logs, pass/fail counters) must be
     * executable AND data-accessible from BOTH the kernel test task and
     * the boxA test entry; any fragment left in kernel-only flash is a
     * user-mode fault. KEEP: referenced by both worlds, but the
     * convention for shared code is KEEP. */
    KEEP(*box_usr_test.*(.text .text.* .rodata .rodata.*))
    /* los_usr_swtmr_task.o (kernel/box:box_usr, refs
     * kernel_s/usr/los_usr_swtmr_task.c): BoxSwtmrTask runs UNPRIVILEGED
     * inside the owning box and reads its own rodata (the task name and
     * the handler-too-slow format string), so the whole object belongs
     * in COMMON. It carries no section attributes of its own, hence the
     * file-name rule - the same shape as box_usr_mem above. Matched by
     * FILE name, not module name, so moving it between modules does not
     * silently drop it back into kernel flash. */
    KEEP(*los_usr_swtmr_task*.o(.text .text.* .rodata .rodata.*))
    /* los_usr_init.o (kernel/box module): __stack_chk_fail runs in USER
     * mode out of .usrtcm.text above, and its rodata (the format string)
     * is read by that mode - user code can neither execute nor READ
     * kernel flash, so the string must sit in COMMON. Collected by file
     * name for the same reason as box_usr_mem: the object is the unit
     * (only the rodata needs it here; the function itself rides the
     * .usrtcm.text attribute, the globals ride .USER_READONLY_DATA). */
    KEEP(*los_usr_init*.o(.rodata .rodata.*))
    /* Kernel-test suites (testsuites/sample/kernel/queue + event, built as
     * static_library members): their .text/.rodata land in COMMON_FLASH so
     * the 128K KERNEL_FLASH slot holds only the kernel proper + the test
     * framework (osTest.c test_init module). EXCLUDE_FILE above drops these
     * members from the KERNEL_FLASH .text/.rodata rules; these rules collect
     * them here. Patterns are the archive MEMBER names (It_los_queue_001.c.o,
     * LLt_/Llt_ variants, It_los_event_NNN.c.o) - the It_ prefix cannot
     * collide with kernel modules like base.los_queue.o. Deliberately NOT
     * KEEP: suite code is kernel-only and every live case is referenced from
     * its suite entry (ItSuiteLosQueue/ItSuiteLosEvent), so --gc-sections
     * drops the cases the suite-list macros gate out (TEST_FULL /
     * QUEUE_STATIC_ALLOCATION), keeping COMMON_TEXT small. Data/BSS stay in
     * KERNEL_RAM: nothing in a box ever touches suite state. */
    *It_los_queue*.o(.text .text.* .rodata .rodata.*)
    *LLt_los_queue*.o(.text .text.* .rodata .rodata.*)
    *Llt_los_queue*.o(.text .text.* .rodata .rodata.*)
    *It_los_event*.o(.text .text.* .rodata .rodata.*)
    /* libc mem/str primitives shared with the box world: the pure
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
    /* Float / 64-bit arithmetic compiler support shared with the box world.
     *
     * Why: the soft-double helpers (__aeabi_d*) and frexp are the only
     * members of the printf closure that are pure math. musl's vfprintf
     * itself stays in kernel flash on purpose - it needs __lockfile ->
     * pthread_mutex_lock -> the posix scheduler surface (ArchIntLock,
     * LOS_Schedule, OsSchedPrioModify, ...) plus __errno_location, strerror
     * and wctomb/locale, none of which have a meaningful unprivileged form.
     * Routing the arithmetic first means a COMMON-side formatter can do
     * double math without dragging any of that in.
     *
     * Audited member by member (objdump -h + nm -u): all 15 are bare .text
     * with ZERO .rodata/.data/.bss, and their only undefined references are
     * inside this same set (__aeabi_ldiv0, __udivmoddi4, __aeabi_d2uiz,
     * __aeabi_dmul, __aeabi_dsub, __aeabi_ui2d) - closed and burden-free.
     * __aeabi_ldiv0 is a "bx lr; nop" stub in _dvmd_tls.o, so even the
     * divide-by-zero path pulls nothing in. frexp/frexpl need only
     * __aeabi_dcmpeq/__aeabi_dmul from the set above.
     *
     * DELIBERATELY NOT ROUTED - the three unwinder members (libunwind.o,
     * pr-support.o, unwind-arm.o): unwind-arm.o reads __exidx_start /
     * __exidx_end (the .ARM.exidx table lives in kernel flash) and
     * pr-support.o calls abort(). Nothing in the box world unwinds
     * (LOSCFG_BACKTRACE_TYPE 1 is a stack scan, -fno-exceptions), so they
     * stay kernel-only: a COMMON member must not drag in anything that
     * lives outside COMMON.
     *
     * These are prebuilt libgcc members: no -ffunction-sections, so the code
     * sits in bare .text and needs explicit FILE rules (same reason as
     * memcpy_le.o / strlen-armv6t2.o above). MAINTAIN THIS LIST: a libgcc
     * helper that COMMON code calls but that was not routed here is a silent
     * user-mode IACCVIOL at run time, not a link error. */
    *(.text.frexp .text.frexpl)
    *_arm_addsubdf3.o(.text)
    *_arm_addsubsf3.o(.text)
    *_arm_cmpdf2.o(.text)
    *_arm_fixdfsi.o(.text)
    *_arm_fixunsdfsi.o(.text)
    *_arm_muldf3.o(.text)
    *_arm_muldivdf3.o(.text)
    *_arm_mulsf3.o(.text)
    *_arm_truncdfsf2.o(.text)
    *_aeabi_ldivmod.o(.text)
    *_aeabi_uldivmod.o(.text)
    *_dvmd_tls.o(.text)
    *_fixunsdfdi.o(.text)
    *_popcountsi2.o(.text)
    *_udivmoddi4.o(.text)
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
    /* Shared-allocator INITIALIZED data would land here (explicit
     * .COMMON_DATA sections). NOTE: zero-initialized objects must NOT be
     * listed here — a NOBITS input section inside this PROGBITS/AT output
     * section shares its LMA slot with other NOBITS sections (ld does not
     * advance the AT cursor), so the startup copy loop would read
     * whatever real flash data occupies that address (caught by a
     * watchpoint: g_MALLOC_HOOK was seeded with string bytes). Zero
     * objects go to .COMMON_BSS below, zeroed by startup. */
    *box_usr_mem.*(.data .data.*)
    /* UTS runner initialized data (normally empty - all runner state is
     * zero-initialized and must land in .COMMON_BSS below per the NOBITS
     * note above). */
    *box_usr_test.*(.data .data.*)
    *los_usr_swtmr_task*.o(.data .data.*)
    . = ALIGN(0x4);
    __common_data_end = .;
  } > COMMON_RAM AT> KERNEL_FLASH
  __common_data_load = LOADADDR(.COMMON_DATA);

  .COMMON_BSS (NOLOAD) : ALIGN(0x4) {
    __common_bss_start = .;
    *(.COMMON_BSS .COMMON_BSS.*)
    /* Shared-allocator zero-initialized data: g_MALLOC_HOOK /
     * m_aucSysMem* / g_poolHead — zeroed by the startup common_bss loop
     * (COMMON_RAM is outside the __bss range). */
    *box_usr_mem.*(.bss .bss.* COMMON)
    /* UTS runner zero-initialized state: iCunit case/suit arrays, module
     * stats, fail logs, the shared g_passResult/g_failResult counters and
     * the two-phase flag g_utsPhase - zeroed by the startup common_bss
     * loop (COMMON_RAM is outside the __bss range). */
    *box_usr_test.*(.bss .bss.* COMMON)
    *los_usr_swtmr_task*.o(.bss .bss.* COMMON)
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

#===============================================================================
# Hand-maintained companion of sources.cmake (auto-generated; regeneration
# overwrites it — never merge content into it). Guards are re-resolved per
# board by gate sync; contracts overridable before add_subdirectory().
#===============================================================================

# kernel/base/BUILD.gn in-target gates the 3322 snapshot flattened away;
# re-gated here so each board follows its own target_config.h.
list(REMOVE_ITEM EVOLVE_KERNEL_SOURCES
    ${EVOLVE_TOP_DIR}/kernel/base/mem/common/memstat/los_memstat.c
    ${EVOLVE_TOP_DIR}/kernel/base/mem/slab/los_slab.c
    ${EVOLVE_TOP_DIR}/kernel/base/mem/slab/los_slabmem.c)

if(LOSCFG_MEM_TASK_STAT)
    list(APPEND EVOLVE_KERNEL_SOURCES
        ${EVOLVE_TOP_DIR}/kernel/base/mem/common/memstat/los_memstat.c)
endif()

if(LOSCFG_KERNEL_MEM_SLAB_EXTENTION)
    list(APPEND EVOLVE_KERNEL_SOURCES
        ${EVOLVE_TOP_DIR}/kernel/base/mem/slab/los_slab.c
        ${EVOLVE_TOP_DIR}/kernel/base/mem/slab/los_slabmem.c)
endif()

# Board trap/driver sources outside the GN graph. 3322 default.
if(NOT DEFINED EVOLVE_EXTRA_SOURCES)
    set(EVOLVE_EXTRA_SOURCES
        ${EVOLVE_TOP_DIR}/arch/risc-v/src/los_trap.S
        ${EVOLVE_TOP_DIR}/drivers/interrupt/riscv_lcmp_clic.c
        ${EVOLVE_TOP_DIR}/drivers/timer/dw_apb_timer.c)
endif()
list(APPEND EVOLVE_KERNEL_SOURCES ${EVOLVE_EXTRA_SOURCES})

# Upstream musl src/ files the snapshot never walks.  NOT imported — shimmed
# in vendor_libc_compat.c instead: iswlower.c, sysconf.c, vasprintf.c,
# setvbuf.c, open_memstream.c.  Appended inside `if(LOSCFG_LIBC_MUSL)`.
if(NOT DEFINED EVOLVE_MUSL_EXTRA_SOURCES)
    set(EVOLVE_MUSL_EXTRA_SOURCES
        ${EVOLVE_MUSL_DIR}/porting/liteos_m/kernel/src/stdio/vprintf.c
        ${EVOLVE_MUSL_DIR}/src/stdio/sprintf.c
        ${EVOLVE_MUSL_DIR}/src/stdio/fprintf.c
        ${EVOLVE_MUSL_DIR}/src/stdio/ferror.c
        ${EVOLVE_MUSL_DIR}/src/stdio/setbuf.c
        ${EVOLVE_MUSL_DIR}/src/stdlib/qsort.c
        ${EVOLVE_MUSL_DIR}/src/stdlib/qsort_nr.c
        ${EVOLVE_MUSL_DIR}/src/string/strpbrk.c
        ${EVOLVE_MUSL_DIR}/src/string/strcasestr.c
        ${EVOLVE_MUSL_DIR}/src/time/difftime.c
        ${EVOLVE_MUSL_DIR}/src/multibyte/mbrlen.c
        ${EVOLVE_MUSL_DIR}/src/multibyte/mbsrtowcs.c
        ${EVOLVE_MUSL_DIR}/src/multibyte/mbsnrtowcs.c
        ${EVOLVE_MUSL_DIR}/src/multibyte/wcsnrtombs.c
        ${EVOLVE_MUSL_DIR}/src/setjmp/riscv32/setjmp.S
        ${EVOLVE_MUSL_DIR}/src/setjmp/riscv32/longjmp.S)
endif()

# Hard-exclusion hammer; default EMPTY — the 3322 memstat/slab drops happen
# via the re-gates above (header-silent => OFF).
if(NOT DEFINED EVOLVE_EXCLUDE_SOURCES)
    set(EVOLVE_EXCLUDE_SOURCES "")
endif()

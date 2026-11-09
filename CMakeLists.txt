#===============================================================================
# @brief    liteos_m kernel — source-level CMake integration
#
# Builds the liteos_m kernel from the auto-generated kconfig_values.cmake +
# sources.cmake snapshots. Two integration shapes:
#   * fbb-style hosts (build_component() defined + TARGET_COMPONENT gate):
#     component "liteos_m" (+ liteos_m_interface) via build_component().
#   * plain add_subdirectory() consumers: standalone liteos_m_kernel (STATIC)
#     + liteos_m_kernel_headers (INTERFACE) targets, see the bottom of this
#     file — the docs/integration-guide.md quick-start works as documented.
#
# Config comes from target_config.h (via los_config.h), NOT menuconfig.h —
# see divergence guard below (target_config.h diverges from the GN baseline).
# Module gating ALSO follows target_config.h:
# evolve_sync_gates_from_target_config() (build/cmake/target_config_sync.cmake)
# re-points every LOSCFG_* switch guarding a module in sources.cmake at the
# board header, so the sources built always match the macros compiled.
# kconfig_values.cmake remains the source only for the numeric values the
# divergence guard compares.
#
# All board-specifics are integrator-injected EVOLVE_* variables with defaults
# that reproduce the 3322/hispark_hiditing reference integration, so existing
# fbb builds are byte-identical without changes. Other products override the
# EVOLVE_* vars before add_subdirectory() — see docs/integration-guide.md.
#===============================================================================

set(COMPONENT_NAME "liteos_m")
if(DEFINED TARGET_COMPONENT)
    # Component-gated host (fbb): build this component only when it is on the
    # target's component list (RAM_COMPONENT/ROM_COMPONENT from the host
    # config). Hosts without TARGET_COMPONENT fall through to the standalone
    # liteos_m_kernel/liteos_m_kernel_headers targets at the bottom.
    if(NOT ${COMPONENT_NAME} IN_LIST TARGET_COMPONENT)
        return()
    endif()
endif()

# === Integrator-injected variables (see docs/integration-guide.md) ==========
# Each has a default that reproduces the 3322 reference integration; override
# before add_subdirectory() for a different board.

# --- paths ---
if(NOT DEFINED EVOLVE_TOP_DIR)
    set(EVOLVE_TOP_DIR ${CMAKE_CURRENT_SOURCE_DIR})
endif()
if(NOT DEFINED EVOLVE_SOURCE_ROOT)
    set(EVOLVE_SOURCE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
endif()
# Musl repo root (independent repo, checkout location varies). 3322 default:
# fbb layout (kernel/third_party/musl). Other integrations point this at their
# own musl checkout; subpaths (/porting/liteos_m/kernel/...) are appended below.
if(NOT DEFINED EVOLVE_MUSL_DIR)
    set(EVOLVE_MUSL_DIR ${EVOLVE_SOURCE_ROOT}/third_party/musl)
endif()

# Board glue dir ("liteos_m_port" sibling for 3322). Used for PUBLIC_HEADER
# + ASM -include.
if(NOT DEFINED EVOLVE_BOARD_DIR)
    set(EVOLVE_BOARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../liteos_m_port)
endif()
set(_PORT ${EVOLVE_BOARD_DIR})

# --- kconfig snapshot: sets LOSCFG_* CMake variables (GN baseline) ---
include(${EVOLVE_TOP_DIR}/build/cmake/kconfig_values.cmake)

# --- module gating follows the board header (single source of truth) -------
# kconfig_values.cmake is a frozen GN export of ONE board; the macros the code
# actually compiles with come from ${_TARGET_CONFIG_H} (pulled in by
# kernel/include/los_config.h:40). The sync re-points every LOSCFG_* switch
# guarding a module in sources.cmake at that header, so flipping a feature
# there now adds/drops the module's sources to match — before this, the two
# sides could only drift (SIGNAL/PLATFORM_EXC had to be reconciled by hand
# via EVOLVE_MODULE_DISABLES). Numeric LOSCFG_* keep their snapshot values:
# they are what the divergence guard below compares the header against.
set(_TARGET_CONFIG_H ${_PORT}/include/target_config.h)
include(${EVOLVE_TOP_DIR}/build/cmake/target_config_sync.cmake)
evolve_sync_gates_from_target_config()

# POSIX is kept enabled: lib/posix glue (malloc/errno/lock/time) compiles from
# source alongside the porting musl (LOSCFG_LIBC_MUSL), replacing libc.a.
# EVOLVE_MODULE_DISABLES stays as an explicit force-off hammer that wins over
# the header sync. The 3322 default is now EMPTY: SIGNAL/PLATFORM_EXC are
# silent in target_config.h, so the sync above turns them off automatically.
if(NOT DEFINED EVOLVE_MODULE_DISABLES)
    set(EVOLVE_MODULE_DISABLES "")
endif()
foreach(_m IN LISTS EVOLVE_MODULE_DISABLES)
    set(${_m} "0")
endforeach()

# --- divergence guard: target_config.h MUST override critical values that
# differ from the GN baseline. 3322 default = TICK/TIMESLICE/HWI_PRIO/CPUP.
# Other boards set their own list, or empty to disable the guard entirely. ---
if(NOT DEFINED EVOLVE_DIVERGE_GUARD)
    set(EVOLVE_DIVERGE_GUARD
        LOSCFG_BASE_CORE_TICK_PER_SECOND
        LOSCFG_BASE_CORE_TIMESLICE_TIMEOUT
        LOSCFG_HWI_PRIO_LIMIT
        LOSCFG_CPUP_SAMPLE_PERIOD
        LOSCFG_BASE_IPC_QUEUE_LIMIT)
endif()
if(EXISTS ${_TARGET_CONFIG_H} AND EVOLVE_DIVERGE_GUARD)
    foreach(_sym IN LISTS EVOLVE_DIVERGE_GUARD)
        file(STRINGS ${_TARGET_CONFIG_H} _hit REGEX "^#define ${_sym}[ \t]+")
        if(NOT _hit)
            message(FATAL_ERROR
                "liteos_m divergence guard: ${_sym} NOT defined in ${_TARGET_CONFIG_H}\n"
                "This board MUST override the GN baseline (${${_sym}}).\n"
                "Without it the upstream default leaks → silent runtime drift.\n"
                "Restore the #define in target_config.h, or set EVOLVE_DIVERGE_GUARD\n"
                "to an empty list if your board intentionally matches the baseline.")
        endif()
        string(REGEX MATCH "${_sym}[ \t]+\\(?([0-9]+)" _m "${_hit}")
        set(_actual ${CMAKE_MATCH_1})
        set(_expected ${${_sym}})
        if(_actual STREQUAL _expected)
            message(WARNING
                "liteos_m divergence guard: ${_sym} (${_actual}) now MATCHES\n"
                "the GN baseline (${_expected}). If unintended, restore target_config.h.")
        else()
            message(STATUS
                "liteos_m divergence guard: ${_sym} OK "
                "(target_config.h=${_actual}, baseline=${_expected})")
        endif()
    endforeach()
endif()

# --- sources.cmake: appends to EVOLVE_KERNEL_SOURCES (module-guarded) ---
include(${EVOLVE_TOP_DIR}/build/cmake/sources.cmake)

# Rewrite musl paths: sources.cmake (auto-generated) hard-codes
# ${EVOLVE_SOURCE_ROOT}/third_party/musl/porting/liteos_m/kernel/src/...
# Replace with ${EVOLVE_MUSL_DIR}/porting/liteos_m/kernel so the musl checkout
# can live anywhere (musl is an independent repo). lib/posix paths untouched.
list(TRANSFORM EVOLVE_KERNEL_SOURCES REPLACE
    "${EVOLVE_SOURCE_ROOT}/third_party/musl/porting/liteos_m/kernel"
    "${EVOLVE_MUSL_DIR}/porting/liteos_m/kernel")

# --- gap-fill: board-specific sources not captured by export_cmake_sources.py
# (script only walks the //kernel/liteos_m:libkernel GN graph; board trap/driver
# sources live outside it). 3322 default = los_trap.S + CLIC + dw_apb_timer. ---
if(NOT DEFINED EVOLVE_EXTRA_SOURCES)
    set(EVOLVE_EXTRA_SOURCES
        ${EVOLVE_TOP_DIR}/arch/risc-v/src/los_trap.S
        ${EVOLVE_TOP_DIR}/drivers/interrupt/riscv_lcmp_clic.c
        ${EVOLVE_TOP_DIR}/drivers/timer/dw_apb_timer.c)
endif()
list(APPEND EVOLVE_KERNEL_SOURCES ${EVOLVE_EXTRA_SOURCES})

set(SOURCES ${EVOLVE_KERNEL_SOURCES})

# Extract musl/posix sources into a separate static library (liteos_m_musl).
# src/include/features.h `#define weak ...` would hijack every
# __attribute__((weak)) in kernel/middleware sources if shared.
if(LOSCFG_LIBC_MUSL)
    set(EVOLVE_MUSL_SOURCES "")
    set(EVOLVE_MUSL_REMAINING "")
    foreach(_f IN LISTS EVOLVE_KERNEL_SOURCES)
        if(_f MATCHES "/lib/posix/src/" OR _f MATCHES "/musl/porting/")
            list(APPEND EVOLVE_MUSL_SOURCES ${_f})
        else()
            list(APPEND EVOLVE_MUSL_REMAINING ${_f})
        endif()
    endforeach()
    set(SOURCES ${EVOLVE_MUSL_REMAINING})
    list(REMOVE_DUPLICATES EVOLVE_MUSL_SOURCES)
    # Drop dirname.c: string.h `basename(void)` vs libgen.h `basename(char*)`
    # is a hard conflicting-types error.
    list(FILTER EVOLVE_MUSL_SOURCES EXCLUDE REGEX "/misc/dirname\\.c$")
    # Link-gap additions, gated behind EVOLVE_MUSL_EXTRA_SOURCES (default
    # reproduces the 3322 reference; other boards trim or replace the list —
    # e.g. a non-riscv32 board must drop the setjmp/riscv32 pair):
    # vprintf.c — sources.cmake snapshot omits it; vendor_libc_compat.c's
    # dprintf calls vprintf. printf.c does not exist in the porting tree.
    # --- Link-gap full-tree additions (build 9 -> 10): upstream musl src/
    # files the sources.cmake snapshot never walks (it only walks the porting
    # tree + lib/posix graph, so plain /src/ files never match the musl
    # filter above). They compile against the porting headers: quoted
    # internal includes resolve to the porting src/internal (stdio_impl.h /
    # libc.h) or own-dir (multibyte internal.h) copies, and <features.h>
    # resolves to the porting src/include/features.h (this target's first -I;
    # fopen.c.obj.d proof), so weak_alias (ferror.c, qsort.c) resolves
    # without any stdio_impl.h chain. OH-touched files' param_check blocks
    # are #ifndef __LITEOS__-gated and this target compiles -D__LITEOS__.
    # Deliberately NOT imported (shimmed in liteos_m_port compat/
    # vendor_libc_compat.c instead): ctype/iswlower.c and conf/sysconf.c
    # (syscall/__getauxval dependencies; ASCII and _SC_PAGESIZE shells cover
    # the only callers) and stdio/vasprintf.c (calls the 5-arg OH __vsnprintf
    # that musl.a does not provide; the porting tree ships plain 4-arg
    # vsnprintf) and stdio/setvbuf.c (build 10: the OH variant's FILE
    # surgery needs F_NOBUF, which the porting stdio_impl.h dropped
    # when it reworked buffering; setvbuf is shimmed in the compat
    # file too) and stdio/open_memstream.c (build 10: not a link gap
    # after all, no fbb source references it, and it also needs the
    # upstream libc extern the porting libc.h dropped).
    # riscv32 setjmp/longjmp .S: porting bits/setjmp.h declares
    # __jmp_buf[32], safely above the .S's 19*8-byte footprint. qsort_nr.c
    # supplies the 4-arg qsort on top of qsort.c's __qsort_r; sprintf.c's
    # vsprintf is already in musl.a.
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
    list(APPEND EVOLVE_MUSL_SOURCES ${EVOLVE_MUSL_EXTRA_SOURCES})
endif()


# --- sources to drop (their LOSCFG guards are absent from target_config.h, or
# they duplicate a prebuilt). 3322 default = memstat + slab. Other boards
# override with their own list (or empty to keep all). ---
if(NOT DEFINED EVOLVE_EXCLUDE_SOURCES)
    set(EVOLVE_EXCLUDE_SOURCES
        ${EVOLVE_TOP_DIR}/kernel/base/mem/common/memstat/los_memstat.c
        ${EVOLVE_TOP_DIR}/kernel/base/mem/slab/los_slab.c
        ${EVOLVE_TOP_DIR}/kernel/base/mem/slab/los_slabmem.c)
endif()
if(EVOLVE_EXCLUDE_SOURCES)
    list(REMOVE_ITEM SOURCES ${EVOLVE_EXCLUDE_SOURCES})
endif()

# --- include dirs: port FIRST, then fork-side (port's arch/cache.h shadows
# fork's and adds <stdint.h> needed under -nostdlibinc; both share guards). ---
# porting musl is the SOLE libc (EVOLVE_LIBC_INCLUDE_DIRS): its alltypes.h
# #includes los_event.h, coupling it to the LM kernel. Other boards point
# these at their own libc/lwip trees or set empty to omit.
# External libc/lwip header dirs are injectable. 3322 uses the upstream
# porting musl as the SOLE libc (EVOLVE_SOURCE_ROOT/third_party/musl/...);
# its alltypes.h #includes los_event.h, coupling it to the LM kernel.
# Other boards point EVOLVE_SOURCE_ROOT at their own musl parent dir,
# or set EVOLVE_LIBC_INCLUDE_DIRS directly. Set empty to omit.
if(NOT DEFINED EVOLVE_LIBC_INCLUDE_DIRS)
    set(EVOLVE_LIBC_INCLUDE_DIRS
        ${EVOLVE_MUSL_DIR}/porting/liteos_m/kernel/include)
endif()
if(NOT DEFINED EVOLVE_LWIP_INCLUDE_DIRS)
    if(DEFINED ROOT_DIR)
        set(EVOLVE_LWIP_INCLUDE_DIRS
            ${ROOT_DIR}/ohos/third_party/lwip/lwip_v2.1.3/src/include
            ${ROOT_DIR}/ohos/third_party/lwip/lwip_adapter/liteos/src/include)
    else()
        # Plain-CMake integrator without the fbb ROOT_DIR layout: omit lwip
        # headers (only TUs that include lwip need them; supply your own via
        # EVOLVE_LWIP_INCLUDE_DIRS if your tree requires them).
        set(EVOLVE_LWIP_INCLUDE_DIRS "")
    endif()
endif()
# securec.h location for the musl target's lib/posix sources. The default is
# the fbb middleware hwsec_c component — it crosses the kernel-repo boundary
# (middleware/ is an fbb tree), so it is injectable for other integrations.
# Set empty to omit.
if(NOT DEFINED EVOLVE_SECUREC_INCLUDE_DIR)
    set(EVOLVE_SECUREC_INCLUDE_DIR
        ${EVOLVE_SOURCE_ROOT}/../middleware/utils/hwsec_c/include)
endif()
# Prepend the libc++ headers AHEAD of the libc include dirs. libc++ ships its
# own <stddef.h>/<stdint.h> wrappers in c++/v1: they include_next into the
# libc copy and then add what C++ needs (e.g. ::nullptr_t at c++11+).
# -nostdinc drops the clang builtin wrappers, so plain <stddef.h> resolves
# over -I only - if the libc dir wins that lookup, libc++'s <cstddef> dies
# on 'using ::nullptr_t' (watchface-lm hit exactly that via <memory>:
# "no member named 'nullptr_t' in the global namespace"). Vendor parity:
# the vendor kernel's liteos_common interface kept c++/v1 ahead of its musl
# too. CLANG_CXX_V1_INCLUDE is set by build/toolchains/clang_riscv_linx_26_08_1.cmake.
if(DEFINED CLANG_CXX_V1_INCLUDE)
    list(PREPEND EVOLVE_LIBC_INCLUDE_DIRS ${CLANG_CXX_V1_INCLUDE})
endif()
set(PUBLIC_HEADER
    # board-side headers (from EVOLVE_BOARD_DIR — precede fork dirs)
    ${_PORT}/include
    ${_PORT}/board
    ${_PORT}/board/asm
    # fork-side include dirs (aggregated from each module's BUILD.gn public config)
    ${EVOLVE_TOP_DIR}/kernel/include
    ${EVOLVE_TOP_DIR}/kernel/base/include
    ${EVOLVE_TOP_DIR}/kernel/base/mem/bestfit
    ${EVOLVE_TOP_DIR}/kernel/extended/backtrace
    ${EVOLVE_TOP_DIR}/kernel/extended/trace
    ${EVOLVE_TOP_DIR}/kernel/extended/trace/cnv
    ${EVOLVE_TOP_DIR}/kernel/extended/cpup
    ${EVOLVE_TOP_DIR}/arch/include
    ${EVOLVE_TOP_DIR}/arch/risc-v/src
    ${EVOLVE_TOP_DIR}/arch/risc-v/include
    ${EVOLVE_TOP_DIR}/arch/risc-v/include/arch
    ${EVOLVE_TOP_DIR}/arch/risc-v/common
    ${EVOLVE_TOP_DIR}/compat/cmsis
    ${EVOLVE_TOP_DIR}/drivers/interrupt/include
    ${EVOLVE_TOP_DIR}/drivers/timer/include
    ${EVOLVE_TOP_DIR}/lib/posix/include
    ${EVOLVE_TOP_DIR}/shell/include
    ${EVOLVE_TOP_DIR}/fs/vfs
    ${EVOLVE_LIBC_INCLUDE_DIRS}
    ${EVOLVE_LWIP_INCLUDE_DIRS}
)

# lwip config defines (mirroring vendor kernel's public interface).
# CHIP_<NAME> defines are NOT injected here — integrators supply them via
# CCFLAGS (e.g. fbb defines CHIP_3322). Adding a kernel-side CHIP_* would
# collide with the integrator's own define (-Werror,-Wmacro-redefined).
set(PUBLIC_DEFINES
    LWIP_CONFIG_FILE=\"lwip/lwipopts_default.h\"
    LWIP_SHARED_CONFIG_FILE=\"lwip/lwipopts_shared.h\"
)

# musl upstream headers trip -Werror (unparenthesized ops, NULL redefinition).
# Scoped to LOSCFG_LIBC_MUSL so the prebuilt-libc.a path stays byte-identical.
if(LOSCFG_LIBC_MUSL)
    set(COMPONENT_PUBLIC_CCFLAGS
        -Wno-error=bitwise-op-parentheses
        -Wno-error=shift-op-parentheses
        -Wno-error=macro-redefined
        -Wno-error=int-conversion
        -Wno-error=incompatible-pointer-types
        -Wno-error=implicit-function-declaration)
endif()

# -march/-mabi are inherited from the integrator's CCFLAGS (build_component.cmake:308).
# Hardcoding them here would diverge from the rest of the firmware and break
# the "one ABI" guarantee. ASM TUs get -include lm_asm_config.h (assembly-safe
# minimal config; target_config.h pulls musl typedefs into asm).
set(COMPONENT_CCFLAGS
    "$<$<COMPILE_LANGUAGE:ASM>:-include${_PORT}/include/lm_asm_config.h>"
)

if(COMMAND build_component)
    build_component()
else()
    # --- standalone / plain-CMake integration --------------------------------
    # No build_component() infrastructure: any CMake project integrates
    # liteos_m with a bare add_subdirectory(). Provides the targets the
    # integration-guide.md quick-start documents:
    #   liteos_m_kernel          STATIC    — kernel + glue sources (${SOURCES})
    #   liteos_m_kernel_headers  INTERFACE — public headers/defines for
    #                                        consumers that only need headers
    # ABI flags (-march/-mabi/--target) come from the integrator's toolchain
    # file / CMAKE_C_FLAGS — the same "one ABI with the rest of the firmware"
    # rule as the fbb path (build_component.cmake:308 there).
    add_library(liteos_m_kernel STATIC ${SOURCES})
    target_include_directories(liteos_m_kernel PUBLIC ${PUBLIC_HEADER})
    # __LITEOS__/__LITEOS_M__ mirror the GN misc_config globals the fbb path
    # injects through its own ${DEFINES} (target_config.py define list).
    target_compile_definitions(liteos_m_kernel PUBLIC
        ${PUBLIC_DEFINES} __LITEOS__ __LITEOS_M__)
    target_compile_options(liteos_m_kernel PRIVATE ${COMPONENT_CCFLAGS})
    add_library(liteos_m_kernel_headers INTERFACE)
    target_include_directories(liteos_m_kernel_headers INTERFACE ${PUBLIC_HEADER})
    target_compile_definitions(liteos_m_kernel_headers INTERFACE
        ${PUBLIC_DEFINES} __LITEOS__ __LITEOS_M__)
    if(LOSCFG_LIBC_MUSL)
        # musl upstream headers trip -Werror on consumers too — keep the
        # suppression interface-visible, mirroring the fbb _interface target.
        target_compile_options(liteos_m_kernel PUBLIC ${COMPONENT_PUBLIC_CCFLAGS})
        target_compile_options(liteos_m_kernel_headers INTERFACE
            ${COMPONENT_PUBLIC_CCFLAGS})
    endif()
endif()

# liteos_m_musl: isolated include scope (src/include + src/internal musl-only).
if(LOSCFG_LIBC_MUSL AND EVOLVE_MUSL_SOURCES)
    add_library(liteos_m_musl STATIC ${EVOLVE_MUSL_SOURCES})
    target_include_directories(liteos_m_musl PRIVATE
        ${EVOLVE_MUSL_DIR}/porting/liteos_m/kernel/src/include
        ${EVOLVE_MUSL_DIR}/porting/liteos_m/kernel/src/internal
        ${EVOLVE_MUSL_DIR}/porting/liteos_m/kernel/include
        ${EVOLVE_TOP_DIR}/kernel/include
        ${EVOLVE_TOP_DIR}/kernel/base/include
        ${EVOLVE_TOP_DIR}/kernel/base/mem/bestfit
        ${EVOLVE_TOP_DIR}/kernel/extended/backtrace
        ${EVOLVE_TOP_DIR}/kernel/extended/trace
        ${EVOLVE_TOP_DIR}/kernel/extended/trace/cnv
        ${EVOLVE_TOP_DIR}/kernel/extended/cpup
        ${EVOLVE_TOP_DIR}/arch/include
        ${EVOLVE_TOP_DIR}/arch/risc-v/src
        ${EVOLVE_TOP_DIR}/arch/risc-v/include
        ${EVOLVE_TOP_DIR}/arch/risc-v/include/arch
        ${EVOLVE_TOP_DIR}/arch/risc-v/common
        ${EVOLVE_TOP_DIR}/compat/cmsis
        ${EVOLVE_TOP_DIR}/drivers/interrupt/include
        ${EVOLVE_TOP_DIR}/drivers/timer/include
        ${EVOLVE_TOP_DIR}/lib/posix/include
        ${EVOLVE_TOP_DIR}/kernel/extended/signal
        ${EVOLVE_TOP_DIR}/kernel/extended/include
        ${_PORT}/include
        ${_PORT}/board
        ${_PORT}/board/asm
        # securec.h: lib/posix sources include it; lives in middleware hwsec_c
        # (fbb default, injectable via EVOLVE_SECUREC_INCLUDE_DIR).
        ${EVOLVE_SECUREC_INCLUDE_DIR}
        ${EVOLVE_LIBC_INCLUDE_DIRS}
        ${EVOLVE_LWIP_INCLUDE_DIRS}
        # Full-tree arch dir at the very END (lowest precedence):
        # qsort.c's OH variant includes the porting
        # src/internal/atomic.h, whose "atomic_arch.h" only exists
        # in arch/riscv32 (lr/sc inline atomics). Every porting dir
        # above still wins anything it provides (notably
        # bits/alltypes.h and bits/setjmp.h), so no existing
        # include resolution changes.
        ${EVOLVE_MUSL_DIR}/arch/riscv32)
    # ${DEFINES} is the fbb global define list (__LITEOS__, CHIP_*, ...). A
    # plain-CMake integrator has no DEFINES — fall back to the GN misc_config
    # set (__LITEOS__/__LITEOS_M__), which musl's OH-touched param_check
    # blocks are gated on.
    if(DEFINED DEFINES)
        target_compile_definitions(liteos_m_musl PRIVATE
            ${PUBLIC_DEFINES} ${DEFINES})
    else()
        target_compile_definitions(liteos_m_musl PRIVATE
            ${PUBLIC_DEFINES} __LITEOS__ __LITEOS_M__)
    endif()
    # Undef _ALL_SOURCE: features.h turns it into _GNU_SOURCE, which activates
    # stdio.h's `#define fopen64 fopen` and breaks fopen.c's weak_alias.
    target_compile_options(liteos_m_musl PRIVATE
        -U_ALL_SOURCE -U_GNU_SOURCE -U_LARGEFILE64_SOURCE)
    # Fail fast with an actionable message instead of a wall of compile
    # errors in lib/posix (every TU includes securec.h).
    if(LOSCFG_LIBC_MUSL AND NOT EXISTS ${EVOLVE_SECUREC_INCLUDE_DIR}/securec.h)
        message(WARNING
            "liteos_m_musl: securec.h not found under "
            "${EVOLVE_SECUREC_INCLUDE_DIR} —\n"
            "lib/posix sources include securec.h and will not compile.\n"
            "Set EVOLVE_SECUREC_INCLUDE_DIR to your securec include dir.")
    endif()
    # ${COMPILE_CCFLAGS} is the fbb-propagated global ${CCFLAGS} (ABI options;
    # build_component.cmake:308). Standalone integrators leave it empty and
    # provide ABI flags via their toolchain file / CMAKE_C_FLAGS instead.
    # musl upstream code trips -Werror (unparenthesized ops, sign compares,
    # library-function redeclarations). Suppress; musl sources are upstream.
    target_compile_options(liteos_m_musl PRIVATE ${COMPILE_CCFLAGS}
        -Wno-error
        -Wno-incompatible-library-redeclaration
        -Wno-incompatible-function-pointer-types
        -Wno-int-conversion
        -Wno-implicit-function-declaration
        -Wno-incompatible-pointer-types)
endif()
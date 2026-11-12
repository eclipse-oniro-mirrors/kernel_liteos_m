#===============================================================================
# liteos_m — source-level CMake integration. Auto-generated snapshots:
# kconfig_values.cmake + sources.cmake; hand-maintained contracts:
# build/cmake/sources_extra.cmake. Two integration shapes:
#   * fbb hosts (build_component() + TARGET_COMPONENT): component "liteos_m".
#   * plain add_subdirectory(): standalone liteos_m_kernel (STATIC) +
#     liteos_m_kernel_headers (INTERFACE) targets at the bottom.
# Config comes from the board's target_config.h, NOT menuconfig.h: gate sync
# (target_config_sync.cmake) re-points every module gate in sources*.cmake at
# the board header; kconfig_values.cmake stays the source only for the numeric
# values the divergence guard compares. Board specifics are EVOLVE_* variables
# with defaults reproducing the 3322 reference integration.
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

# Module gates follow the board header (single source of truth — see the
# file banner); numeric LOSCFG_* keep snapshot values for the guard below.
set(_TARGET_CONFIG_H ${_PORT}/include/target_config.h)
include(${EVOLVE_TOP_DIR}/build/cmake/target_config_sync.cmake)
evolve_sync_gates_from_target_config()

# EVOLVE_MODULE_DISABLES: force-off hammer winning over the header sync
# (3322 default EMPTY — SIGNAL/PLATFORM_EXC are header-silent => auto-OFF).
if(NOT DEFINED EVOLVE_MODULE_DISABLES)
    set(EVOLVE_MODULE_DISABLES "")
endif()
foreach(_m IN LISTS EVOLVE_MODULE_DISABLES)
    set(${_m} "0")
endforeach()

# --- divergence guard: target_config.h MUST override critical values that
# differ from the GN baseline. 3322 default = the 5 symbols below. Other
# boards set their own list, or empty to disable the guard entirely. ---
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

# Hand-maintained contracts. Included after the path rewrite above so
# REMOVE_ITEM matches the final snapshot paths.
include(${EVOLVE_TOP_DIR}/build/cmake/sources_extra.cmake)

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
    # Contract list in build/cmake/sources_extra.cmake.
    list(APPEND EVOLVE_MUSL_SOURCES ${EVOLVE_MUSL_EXTRA_SOURCES})
endif()


# EVOLVE_EXCLUDE_SOURCES contract lives in sources_extra.cmake.
if(EVOLVE_EXCLUDE_SOURCES)
    list(REMOVE_ITEM SOURCES ${EVOLVE_EXCLUDE_SOURCES})
endif()

# Include dirs: port FIRST (its arch/cache.h shadows fork's and provides
# <stdint.h> under -nostdlibinc). The 3322 default libc is the porting musl
# (sole libc; alltypes.h couples it to the LM kernel); other boards point
# EVOLVE_LIBC/LWIP_INCLUDE_DIRS at their own trees, or empty to omit.
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
# Prepend libc++ headers AHEAD of the libc dirs: their <stddef.h> wrappers
# include_next into libc and add ::nullptr_t (c++11+); with -nostdinc, -I is
# the only lookup — if libc wins it, <cstddef> breaks (watchface-lm hit this
# via <memory>). CLANG_CXX_V1_INCLUDE comes from the clang toolchain file.
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

# lwip config defines (mirroring vendor kernel's public interface) — the
# EVOLVE_PUBLIC_DEFINES contract: whole-list override, empty to omit.
# CHIP_* stays out: integrators supply it via CCFLAGS (fbb: CHIP_3322);
# a kernel-side copy would collide (-Werror,-Wmacro-redefined).
if(NOT DEFINED EVOLVE_PUBLIC_DEFINES)
    set(EVOLVE_PUBLIC_DEFINES
        LWIP_CONFIG_FILE=\"lwip/lwipopts_default.h\"
        LWIP_SHARED_CONFIG_FILE=\"lwip/lwipopts_shared.h\"
    )
endif()
# Bridge to the unprefixed convention name build_component() reads.
set(PUBLIC_DEFINES ${EVOLVE_PUBLIC_DEFINES})

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
    # Standalone: liteos_m_kernel (STATIC, ${SOURCES}) + liteos_m_kernel_headers
    # (INTERFACE) — the integration-guide.md quick-start targets. ABI flags
    # come from the integrator's toolchain / CMAKE_C_FLAGS, same "one ABI"
    # rule as the fbb path (build_component.cmake:308).
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
        # Full-tree arch dir LAST (lowest precedence): qsort.c's OH variant
        # needs atomic_arch.h, which only exists here — every porting dir
        # above still wins (notably bits/alltypes.h, bits/setjmp.h).
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
    # ${COMPILE_CCFLAGS}: fbb-propagated ABI flags (standalone leaves it
    # empty — flags come from the integrator's toolchain instead).
    # -Wno-*: musl upstream code trips -Werror.
    target_compile_options(liteos_m_musl PRIVATE ${COMPILE_CCFLAGS}
        -Wno-error
        -Wno-incompatible-library-redeclaration
        -Wno-incompatible-function-pointer-types
        -Wno-int-conversion
        -Wno-implicit-function-declaration
        -Wno-incompatible-pointer-types)
endif()
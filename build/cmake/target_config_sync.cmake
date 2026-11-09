#===============================================================================
# @brief    liteos_m — target_config.h is the single source of truth for the
#           LOSCFG_* MODULE GATES in build/cmake/sources.cmake.
#
# Problem this fixes: kconfig_values.cmake is a frozen GN snapshot of ONE
# board (3322). It only picks source files; the feature macros the code
# actually compiles with come from ${_TARGET_CONFIG_H} (the board header
# pulled in by kernel/include/los_config.h:40). Without a sync the two sides
# drifted silently: flipping a switch in target_config.h did NOT add/drop
# the module's sources, and the 3322 gaps (LOSCFG_KERNEL_SIGNAL /
# LOSCFG_PLATFORM_EXC: snapshot=on, header=off) had to be reconciled by a
# hand-maintained EVOLVE_MODULE_DISABLES list. That made the "kconfig"
# decorative — it did not control the features liteos_m provides.
#
# Semantics — for every LOSCFG_* symbol that guards a module in
# build/cmake/sources.cmake (the gate set is parsed from that file, so new
# guards are picked up automatically):
#
#   target_config.h `#define SYM 1`  -> gate ON  (header wins over snapshot)
#   target_config.h `#define SYM 0`  -> gate OFF
#   target_config.h silent           -> gate OFF — mirrors the compile-time
#                        truth: los_config.h's #ifndef defaults are OFF
#                        (e.g. LOSCFG_KERNEL_SIGNAL defaults to 0), and an
#                        undefined macro fails #if/#ifdef alike.
#   SYM defined but not 0/1-valued   -> keep snapshot value + WARNING
#
# Precedence (applied by the caller, liteos_m/CMakeLists.txt):
#   EVOLVE_MODULE_DISABLES (integrator) > target_config.h > snapshot.
# There is deliberately no force-ENABLE override: sources without the
# matching compiled macros (or vice versa) is exactly the drift this sync
# exists to prevent — to enable a module, define it in target_config.h.
#
# Numeric/string LOSCFG_* (limits, periods, stack sizes) keep their snapshot
# values: they are inputs to the divergence guard, which compares them
# against the header itself.
#
# Contract with the caller (function: locals vanish on return, gate symbols
# are written back to the caller's scope via PARENT_SCOPE):
#   reads  _TARGET_CONFIG_H — board header path (${EVOLVE_BOARD_DIR}/...)
#   reads  EVOLVE_TOP_DIR   — liteos_m source root
#   writes every gate symbol (LOSCFG_*) as a variable in the caller's scope
#===============================================================================

function(evolve_sync_gates_from_target_config)
    if(NOT EXISTS "${_TARGET_CONFIG_H}")
        message(WARNING
            "liteos_m gate sync: ${_TARGET_CONFIG_H} not found — module\n"
            "gating keeps the frozen kconfig_values.cmake snapshot as-is.\n"
            "Without the board header the compiled macros come from\n"
            "los_config.h defaults (mostly OFF), which may not match the\n"
            "sources being built. Point EVOLVE_BOARD_DIR at a board dir with\n"
            "include/target_config.h (see build/cmake/board_template/).")
        return()
    endif()

    # -- 1) gate set: every LOSCFG_* tested in sources.cmake if() guards ----
    file(STRINGS "${EVOLVE_TOP_DIR}/build/cmake/sources.cmake"
        _guard_lines REGEX "^if\\(LOSCFG_")
    set(_gates "")
    foreach(_line IN LISTS _guard_lines)
        string(REGEX MATCHALL "LOSCFG_[A-Z0-9_]+" _syms "${_line}")
        list(APPEND _gates ${_syms})
    endforeach()
    if(_gates)
        list(REMOVE_DUPLICATES _gates)
    endif()

    # -- 2) one pass over the board header ----------------------------------
    # Every LOSCFG_* mention marks _seen_<SYM>. When the define is a clean
    # boolean — "#define SYM 0/1" and nothing else on the line — its value
    # lands in _val_<SYM>. Anything else (5, "(1000UL)", a trailing comment)
    # leaves _val_ unset: "seen but not boolean", handled in step 3.
    file(STRINGS "${_TARGET_CONFIG_H}" _hdr_lines
        REGEX "^#define[ \t]+LOSCFG_[A-Z0-9_]+[ \t]+")
    foreach(_line IN LISTS _hdr_lines)
        string(REGEX MATCH
            "^#define[ \t]+(LOSCFG_[A-Z0-9_]+)" _match "${_line}")
        set(_seen_${CMAKE_MATCH_1} 1)
        string(REGEX MATCH
            "^#define[ \t]+(LOSCFG_[A-Z0-9_]+)[ \t]+([01])[ \t]*$" _match "${_line}")
        if(_match)
            set(_val_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}")
        endif()
    endforeach()

    # -- 3) apply: header wins, header-silent => OFF ------------------------
    set(_changed "")
    foreach(_gate IN LISTS _gates)
        if(DEFINED _val_${_gate})
            set(_new "${_val_${_gate}}")
        elseif(NOT DEFINED _seen_${_gate})
            set(_new "0")
        else()
            message(WARNING
                "liteos_m gate sync: ${_gate} is defined in ${_TARGET_CONFIG_H}\n"
                "but not boolean (0/1) — keeping snapshot value '${${_gate}}'.")
            continue()
        endif()

        # snapshot value; an undefined one reads as OFF, like the silent case
        set(_old "${${_gate}}")
        if("${_old}" STREQUAL "")
            set(_old "0")
        endif()
        if(NOT "${_old}" STREQUAL "${_new}")
            set(_changed "${_changed}${_gate}:${_old}->${_new}  ")
        endif()
        set(${_gate} "${_new}" PARENT_SCOPE)
    endforeach()

    list(LENGTH _gates _gate_count)
    if(_changed)
        message(STATUS
            "liteos_m gate sync: ${_gate_count} module gates now follow "
            "${_TARGET_CONFIG_H} (changed: ${_changed})")
    else()
        message(STATUS
            "liteos_m gate sync: ${_gate_count} module gates match "
            "${_TARGET_CONFIG_H}")
    endif()
endfunction()

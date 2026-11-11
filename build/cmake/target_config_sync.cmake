#===============================================================================
# target_config.h is the single source of truth for the LOSCFG_* module gates
# in sources.cmake / sources_extra.cmake (gate set parsed from those files):
#
#   header `#define SYM 1`  -> gate ON   (header wins over the snapshot)
#   header `#define SYM 0`  -> gate OFF
#   header silent           -> gate OFF  (los_config.h #ifndef defaults are
#                              OFF; an undefined macro fails #if/#ifdef alike)
#   SYM defined, not 0/1    -> keep snapshot value + WARNING
#
# No force-ENABLE override on purpose: enabling a module = defining it in
# target_config.h. Numeric/string LOSCFG_* keep snapshot values (divergence
# guard inputs). Caller contract — reads _TARGET_CONFIG_H, EVOLVE_TOP_DIR;
# writes every gate symbol into the caller's scope (PARENT_SCOPE).
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

    # -- 1) gate set: every LOSCFG_* tested in an if() guard of sources*.cmake
    set(_GATES "")
    foreach(_GF "${EVOLVE_TOP_DIR}/build/cmake/sources.cmake"
                "${EVOLVE_TOP_DIR}/build/cmake/sources_extra.cmake")
        if(EXISTS "${_GF}")
            file(STRINGS "${_GF}" _LINES REGEX "^if\\(LOSCFG_")
            foreach(_LINE IN LISTS _LINES)
                string(REGEX MATCHALL "LOSCFG_[A-Z0-9_]+" _SYMS "${_LINE}")
                list(APPEND _GATES ${_SYMS})
            endforeach()
        endif()
    endforeach()
    if(_GATES)
        list(REMOVE_DUPLICATES _GATES)
    endif()

    # -- 2) re-point each gate at the board header — same per-symbol lookup
    # as the divergence guard (CMakeLists.txt): header-silent => OFF, clean
    # boolean "#define SYM 0/1" => value, non-boolean => keep snapshot + warn.
    set(_CHANGED "")
    foreach(_GATE IN LISTS _GATES)
        file(STRINGS "${_TARGET_CONFIG_H}" _HIT
            REGEX "^#define[ \t]+${_GATE}[ \t]+")
        if(NOT _HIT)                                          # header-silent
            set(_NEW "0")
        elseif(_HIT MATCHES "^#define[ \t]+${_GATE}[ \t]+([01])[ \t]*$")
            set(_NEW "${CMAKE_MATCH_1}")                      # header wins
        else()
            message(WARNING
                "liteos_m gate sync: ${_GATE} is defined in ${_TARGET_CONFIG_H}\n"
                "but not boolean (0/1) — keeping snapshot value '${${_GATE}}'.")
            continue()                                        # keep snapshot
        endif()

        set(_OLD "${${_GATE}}")                               # unset reads as OFF
        if("${_OLD}" STREQUAL "")
            set(_OLD "0")
        endif()
        if(NOT "${_OLD}" STREQUAL "${_NEW}")
            string(APPEND _CHANGED "${_GATE}:${_OLD}->${_NEW}  ")
        endif()
        set(${_GATE} "${_NEW}" PARENT_SCOPE)
    endforeach()

    list(LENGTH _GATES _GATE_COUNT)
    if(_CHANGED)
        message(STATUS
            "liteos_m gate sync: ${_GATE_COUNT} module gates now follow "
            "${_TARGET_CONFIG_H} (changed: ${_CHANGED})")
    else()
        message(STATUS
            "liteos_m gate sync: ${_GATE_COUNT} module gates match "
            "${_TARGET_CONFIG_H}")
    endif()
endfunction()

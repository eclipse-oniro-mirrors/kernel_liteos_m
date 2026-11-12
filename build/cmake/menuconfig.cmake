#===============================================================================
# @brief    liteos_m — generate menuconfig.h from a board Kconfig (.config)
#           fragment, and export LOSCFG_* as CMake variables.
#
# Self-contained: no dependency on any downstream build system. Mirrors what
# GN's `genconfig` produces (`config.h`) so that liteos_m sources compile
# identically under CMake source-level integration.
#
# Inputs (function args):
#   CONFIG   — path to the board .config (LOSCFG_XXX=y / =value lines;
#              "# LOSCFG_XXX is not set" lines are skipped)
#   OUTPUT   — path of the generated menuconfig.h
#
# Effects:
#   * writes ${OUTPUT} with #define LOSCFG_XXX 1 / value
#   * sets LOSCFG_XXX variables in the caller's scope (used by
#     build/cmake/sources.cmake module switches)
#===============================================================================

function(evolve_write_menuconfig_from_scope)
    # Write menuconfig.h from LOSCFG_* variables in the current scope (the
    # GN-expanded snapshot included from kconfig_values.cmake). Only
    # boolean (value "1") and valued symbols are written.
    cmake_parse_arguments(ARG "" "OUTPUT;SOURCE" "" ${ARGN})
    file(WRITE "${ARG_OUTPUT}"
        "/* menuconfig.h — generated from ${ARG_SOURCE} (GN-expanded snapshot) */\n")
    set(_count 0)
    get_cmake_property(_all_vars VARIABLES)
    foreach(_v IN LISTS _all_vars)
        if(_v MATCHES "^LOSCFG_" AND NOT _v MATCHES "^LOSCFG_[A-Z0-9_]*_$")
            set(_val "${${_v}}")
            if(_val STREQUAL "1" OR _val MATCHES "^[0-9]+$" OR _val MATCHES "^[A-Za-z0-9_.:/+=,-]+$")
                file(APPEND "${ARG_OUTPUT}" "#define ${_v} ${_val}\n")
                math(EXPR _count "${_count} + 1")
            endif()
        endif()
    endforeach()
    message(STATUS "liteos_m: menuconfig generated ${ARG_OUTPUT} (${_count} options)")
endfunction()

function(evolve_generate_menuconfig)
    cmake_parse_arguments(ARG "" "CONFIG;OUTPUT" "" ${ARGN})

    if(NOT ARG_CONFIG)
        message(FATAL_ERROR "evolve_generate_menuconfig: CONFIG is required")
    endif()
    if(NOT EXISTS "${ARG_CONFIG}")
        message(FATAL_ERROR
            "evolve_generate_menuconfig: config not found: ${ARG_CONFIG}\n"
            "Set EVOLVE_BOARD_DIR to a board directory containing "
            "kernel_configs/<type>.config")
    endif()

    # Only lines starting with LOSCFG_ are active config; comment lines
    # ("# LOSCFG_XXX is not set") are intentionally ignored.
    file(STRINGS "${ARG_CONFIG}" _los_lines REGEX "^LOSCFG_" ENCODING "UTF-8")

    file(WRITE "${ARG_OUTPUT}" "/* menuconfig.h — generated from ${ARG_CONFIG} by build/cmake/menuconfig.cmake */\n")
    foreach(_line IN LISTS _los_lines)
        # variable name
        string(REGEX MATCH "^[^=]+" _var "${_line}")
        # optional value
        string(REGEX MATCH "=(.+)$" _val "${_line}")
        set(_value "${CMAKE_MATCH_1}")

        if(_value STREQUAL "y")
            file(APPEND "${ARG_OUTPUT}" "#define ${_var} 1\n")
            set(${_var} 1 PARENT_SCOPE)
        else()
            file(APPEND "${ARG_OUTPUT}" "#define ${_var} ${_value}\n")
            # strip quotes for the CMake variable form
            if("${_value}" MATCHES "^\"(.*)\"$")
                set(_value "${CMAKE_MATCH_1}")
            endif()
            set(${_var} "${_value}" PARENT_SCOPE)
        endif()
    endforeach()

    message(STATUS "liteos_m: menuconfig generated ${ARG_OUTPUT} "
                   "(${_los_lines} options) from ${ARG_CONFIG}")
endfunction()

# Copyright (c) Microsoft Corporation.
#
# mwin32 SDK CMake package config.
#
# Installed into the `mwin32_sdk` CPack component as `lib/cmake/m/m-config.cmake`,
# so an external consumer who puts the unpacked SDK root on CMAKE_PREFIX_PATH can
# do `find_package(m CONFIG REQUIRED)` and link `m::mwin32_alias` / `m::m_mwin32`.
#
# This config is deliberately a *relocatable hand-written* config rather than an
# install(EXPORT) dump: the alias is an OBJECT library whose link interface is a
# raw object file plus a generated import library, which export() cannot relocate
# cleanly. Every path below is resolved relative to this file, and the
# per-architecture binary subtree (x64 / arm64) is selected at find_package time.
# See DESIGN-NOTES D-SDK and docs/mwin32-sdk-guide.md §3.

# SDK root is three levels up from lib/cmake/m/.
get_filename_component(_m_sdk_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# Select the per-architecture binary subtree. Honor a caller override
# (-D M_SDK_ARCH=arm64); otherwise infer from the active toolchain.
if(NOT DEFINED M_SDK_ARCH)
    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
        string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" M_SDK_ARCH)
    elseif(CMAKE_GENERATOR_PLATFORM)
        string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" M_SDK_ARCH)
    else()
        set(M_SDK_ARCH "x64")
    endif()
endif()

set(_m_arch_dir "${_m_sdk_root}/${M_SDK_ARCH}")
if(NOT EXISTS "${_m_arch_dir}/bin/m_mwin32.dll")
    message(FATAL_ERROR
        "mwin32 SDK: no binaries for architecture '${M_SDK_ARCH}' under "
        "'${_m_arch_dir}'. Set -D M_SDK_ARCH=<x64|arm64> to match your target.")
endif()

# The shim DLL + its import library and public headers.
if(NOT TARGET m::m_mwin32)
    add_library(m::m_mwin32 SHARED IMPORTED)
    set_target_properties(m::m_mwin32 PROPERTIES
        IMPORTED_LOCATION "${_m_arch_dir}/bin/m_mwin32.dll"
        IMPORTED_IMPLIB   "${_m_arch_dir}/lib/m_mwin32.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_m_sdk_root}/include")
endif()

# The link-time alias. Consumed as an interface so a consumer's link gets:
#   * the alias object file (defines the __imp_<Win32Name> slots that preempt
#     advapi32 / kernel32 — must be an object input, not a static-lib member),
#   * the generated undecorated import library (binds the alias' references to the
#     shim DLL), and
#   * the shim DLL itself (transitively, via m::m_mwin32).
if(NOT TARGET m::mwin32_alias)
    file(GLOB _m_alias_obj "${_m_arch_dir}/lib/objects-*/mwin32_alias/*.obj")
    if(NOT _m_alias_obj)
        message(FATAL_ERROR
            "mwin32 SDK: alias object file missing under '${_m_arch_dir}/lib'.")
    endif()
    add_library(m::mwin32_alias INTERFACE IMPORTED)
    set_target_properties(m::mwin32_alias PROPERTIES
        INTERFACE_LINK_LIBRARIES
            "${_m_alias_obj};${_m_arch_dir}/lib/m_mwin32_alias_import.lib;m::m_mwin32")
endif()

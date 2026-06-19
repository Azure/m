# Copyright (c) Microsoft Corporation.
#
# Merge two (or more) single-architecture mwin32 SDK install trees into one
# multi-architecture SDK matching docs/mwin32-sdk-guide.md §3 (M-SDK-4, D-SDK).
#
# Each input is the install tree produced by installing the `mwin32_sdk` CPack
# component for one architecture: it already has the shared, architecture-neutral
# trees (include/, lib/cmake/, docs/, examples/) plus exactly one architecture
# binary subtree (x64/ or arm64/). The shared trees are byte-identical across
# architectures, so merging is simply: copy every input over the output (shared
# files overwrite identically; the disjoint arch subtrees accumulate).
#
# Usage:
#   cmake -D "SDK_INPUTS=<dir1>;<dir2>" -D SDK_OUTPUT=<out-dir> \
#         -P merge-mwin32-sdk.cmake
#
# After the copy this script asserts the merged tree contains the expected
# architecture subtrees so a broken single-arch input fails the release loudly.

if(NOT DEFINED SDK_INPUTS)
    message(FATAL_ERROR "merge-mwin32-sdk: SDK_INPUTS (a ;-list of install trees) is required.")
endif()
if(NOT DEFINED SDK_OUTPUT)
    message(FATAL_ERROR "merge-mwin32-sdk: SDK_OUTPUT (the merged output dir) is required.")
endif()

file(MAKE_DIRECTORY "${SDK_OUTPUT}")

set(_found_arches "")
foreach(_in IN LISTS SDK_INPUTS)
    if(NOT IS_DIRECTORY "${_in}")
        message(FATAL_ERROR "merge-mwin32-sdk: input '${_in}' is not a directory.")
    endif()

    # Identify which architecture subtree this input carries (the directory that
    # holds bin/m_mwin32.dll).
    set(_this_arch "")
    foreach(_arch x64 arm64 x86)
        if(EXISTS "${_in}/${_arch}/bin/m_mwin32.dll")
            list(APPEND _found_arches "${_arch}")
            set(_this_arch "${_arch}")
        endif()
    endforeach()
    if(NOT _this_arch)
        message(FATAL_ERROR
            "merge-mwin32-sdk: input '${_in}' has no <arch>/bin/m_mwin32.dll; "
            "it is not a valid single-arch SDK tree.")
    endif()

    # Copy the whole input into the output. Shared trees overwrite identically;
    # the arch subtree is unique to this input.
    file(COPY "${_in}/" DESTINATION "${SDK_OUTPUT}")
    message(STATUS "merge-mwin32-sdk: merged '${_this_arch}' tree from ${_in}")
endforeach()

# Sanity-check the merged result: the shared package config and at least one arch
# subtree must be present.
if(NOT EXISTS "${SDK_OUTPUT}/lib/cmake/m/m-config.cmake")
    message(FATAL_ERROR "merge-mwin32-sdk: merged tree is missing lib/cmake/m/m-config.cmake.")
endif()
if(NOT EXISTS "${SDK_OUTPUT}/examples/CMakeLists.txt")
    message(FATAL_ERROR "merge-mwin32-sdk: merged tree is missing examples/CMakeLists.txt.")
endif()

list(REMOVE_DUPLICATES _found_arches)
list(LENGTH _found_arches _arch_count)
message(STATUS "merge-mwin32-sdk: merged ${_arch_count} architecture(s): ${_found_arches}")
message(STATUS "merge-mwin32-sdk: output at ${SDK_OUTPUT}")

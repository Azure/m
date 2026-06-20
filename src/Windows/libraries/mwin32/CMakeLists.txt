cmake_minimum_required(VERSION 3.23)

add_library(m_mwin32 SHARED mwin32.def)

target_compile_features(m_mwin32 PUBLIC ${M_CXX_STD})

add_subdirectory(include)
add_subdirectory(src)
add_subdirectory(test)

# Link-time alias object: clients link this (alongside the m_mwin32 import lib it
# propagates) to redirect their genuine Win32 registry calls into the shim with no
# source edits. The translation unit is generated from mwin32.def so the alias set
# can never drift from the export set. See DESIGN-NOTES D8.
set(mwin32_alias_generator "${CMAKE_CURRENT_SOURCE_DIR}/generate_mwin32_alias.cmake")
set(mwin32_alias_def "${CMAKE_CURRENT_SOURCE_DIR}/mwin32.def")
set(mwin32_alias_source "${CMAKE_CURRENT_BINARY_DIR}/mwin32_alias.cpp")

add_custom_command(
    OUTPUT "${mwin32_alias_source}"
    COMMAND "${CMAKE_COMMAND}"
        "-DMWIN32_DEF=${mwin32_alias_def}"
        "-DMWIN32_ALIAS_OUT=${mwin32_alias_source}"
        -P "${mwin32_alias_generator}"
    DEPENDS "${mwin32_alias_def}" "${mwin32_alias_generator}"
    COMMENT "Generating mwin32 alias translation unit from mwin32.def"
    VERBATIM
)

# Undecorated import library. The shim's mReg* functions have C++ linkage, so the
# auto-generated m_mwin32 import library exposes only their decorated names; the
# alias TU references the undecorated names. The DLL's export table does carry the
# undecorated names (via the .def), so a second import library built from the same
# .def resolves the alias' undecorated references and binds them to the DLL at
# load time. This keeps the shim's source (and its exception behavior) untouched.
# See DESIGN-NOTES D8.
set(mwin32_alias_import "${CMAKE_CURRENT_BINARY_DIR}/m_mwin32_alias_import.lib")

# The /machine flag must follow the active target architecture so the ARM64 build
# emits a correct ARM64 import library (M-SDK-4, D-SDK). MSVC's
# CMAKE_CXX_COMPILER_ARCHITECTURE_ID is x64 / ARM64 / X86 / ARM; lib.exe expects
# the same spellings (case-insensitive).
if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID)
    set(mwin32_alias_machine "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}")
else()
    set(mwin32_alias_machine "x64")
endif()

add_custom_command(
    OUTPUT "${mwin32_alias_import}"
    COMMAND "${CMAKE_LINKER}" /lib /nologo
        "/def:${mwin32_alias_def}"
        "/name:$<TARGET_FILE_NAME:m_mwin32>"
        "/out:${mwin32_alias_import}"
        "/machine:${mwin32_alias_machine}"
    DEPENDS "${mwin32_alias_def}" m_mwin32
    COMMENT "Generating undecorated import library for mwin32 alias from mwin32.def"
    VERBATIM
)

add_custom_target(mwin32_alias_import_lib DEPENDS "${mwin32_alias_import}")

# An OBJECT library so its object files are injected directly into every consumer's
# link (not pulled on demand like a static lib member). This guarantees the __imp_
# slot definitions are always present and preempt advapi32. It links both:
#   * the undecorated import library, which resolves the alias' undecorated shim
#     references and binds them to m_mwin32.dll, and
#   * the m_mwin32 target, so consumers transitively depend on (and copy) the shim
#     DLL at runtime.
add_library(mwin32_alias OBJECT "${mwin32_alias_source}")
target_compile_features(mwin32_alias PUBLIC ${M_CXX_STD})
add_dependencies(mwin32_alias mwin32_alias_import_lib)
target_link_libraries(mwin32_alias PUBLIC m_mwin32 "${mwin32_alias_import}")

# Sample client demonstrating link-time redirection (M-SAMPLE). Added after the
# alias object it links.
add_subdirectory(sample)

list(APPEND m_installation_targets
    m_mwin32
    mwin32_alias
)

# ---------------------------------------------------------------------------
# mwin32 SDK packaging (M-SDK, decision D-SDK).
#
# The shim ships to external consumers as a standalone "mwin32 SDK" — a dedicated
# CPack component (`mwin32_sdk`) carrying the public headers, the per-architecture
# shim binaries, the user's guide, and (M-SDK-3) the examples + CMake package.
# Building/installing the component for x64 and for ARM64 separately and merging
# the two install trees (M-SDK-4) yields the layout documented in
# docs/mwin32-sdk-guide.md §3. The full `m` release keeps installing these targets
# in its own (Unspecified) component via the root install(TARGETS ...).
# ---------------------------------------------------------------------------

# Architecture tag for the per-arch binary subtree. MSVC sets
# CMAKE_CXX_COMPILER_ARCHITECTURE_ID to "x64" / "ARM64" / "X86"; fall back to x64.
string(TOLOWER "${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}" m_sdk_arch)
if(NOT m_sdk_arch)
    set(m_sdk_arch "x64")
endif()

# Shim DLL + auto-generated import library, plus the public headers, into the
# SDK component. Binaries land under <arch>/{bin,lib}; headers are
# architecture-neutral and shared at the SDK root.
install(TARGETS m_mwin32
    RUNTIME DESTINATION "${m_sdk_arch}/bin" COMPONENT mwin32_sdk
    ARCHIVE DESTINATION "${m_sdk_arch}/lib" COMPONENT mwin32_sdk
    FILE_SET HEADERS DESTINATION "include" COMPONENT mwin32_sdk
)

# The link-time alias: its object files (so a consumer's link gets the __imp_
# slots) and its generated undecorated import library (a plain file, not a target
# artifact). Both are architecture-specific.
install(TARGETS mwin32_alias
    OBJECTS DESTINATION "${m_sdk_arch}/lib" COMPONENT mwin32_sdk
)
install(FILES "${mwin32_alias_import}"
    DESTINATION "${m_sdk_arch}/lib" COMPONENT mwin32_sdk
)

# The user's guide travels with the SDK at its documented location.
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/mwin32-sdk-guide.md"
    DESTINATION "docs" COMPONENT mwin32_sdk
)

# Relocatable CMake package config so a consumer can `find_package(m CONFIG)` and
# link `m::mwin32_alias` against the unpacked SDK (M-SDK-3). Installed as
# m-config.cmake under lib/cmake/m. It is architecture-neutral (it selects the
# x64 / arm64 binary subtree at find_package time).
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mwin32-sdk-config.cmake"
    DESTINATION "lib/cmake/m" RENAME "m-config.cmake" COMPONENT mwin32_sdk
)

# The bundled examples: the sample clients as sources (the three Win32 redirect
# samples plus the WC-6/WC-7 raw-Winsock HTTP server/client pair), the
# hand-authored reference OpenAPI contract for the HTTP samples, plus the
# top-level CMakeLists that builds them against the installed package
# (out-of-tree). They consume the SDK exactly as an external user would.
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/sample/mwin32_sample_client.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/sample/mwin32_fs_sample_client.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/sample/mwin32_notify_sample_client.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/sample/mwin32_http_server_sample.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/sample/mwin32_http_client_sample.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/sample/reference-openapi.yaml"
    DESTINATION "examples" COMPONENT mwin32_sdk
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdk-examples-CMakeLists.txt"
    DESTINATION "examples" RENAME "CMakeLists.txt" COMPONENT mwin32_sdk
)

set(m_installation_targets ${m_installation_targets} PARENT_SCOPE)

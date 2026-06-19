# Copyright (c) Microsoft Corporation.
#
# Generates the mwin32 alias translation unit from mwin32.def.
#
# Usage:
#   cmake -DMWIN32_DEF=<path-to-mwin32.def> \
#         -DMWIN32_ALIAS_OUT=<path-to-mwin32_alias.cpp> \
#         -P generate_mwin32_alias.cmake
#
# mwin32.def's EXPORTS list is the SINGLE SOURCE OF TRUTH for the alias set. For
# every exported shim name `m<Name>` this emits a redirect of the genuine Win32
# name `<Name>` (the map is mechanical: strip the leading 'm') onto the shim:
#
#   extern "C" void m<Name>();                            // import thunk (m_mwin32.lib)
#   extern "C" void (*__imp_<Name>)() = &m<Name>;         // the decisive redirect
#   #pragma comment(linker, "/alternatename:<Name>=m<Name>")  // best-effort fallback
#
# A `.def` line may carry a trailing `; noalias` comment. Such a name is still
# exported by the DLL (and present in the undecorated import library) so a client
# may call it explicitly, but it is deliberately NOT auto-redirected here. This
# is how `CloseHandle` opts out: redirecting every `CloseHandle` in a client
# would capture non-shim (real OS) handles, so aliasing it is opt-in.
#
# Why this exact form (see DESIGN-NOTES D8 for the full rationale and the spikes
# that established it):
#   * The shim's `mReg*` functions have C++ linkage, so the auto-generated
#     `m_mwin32` import library exposes only their decorated names. The alias TU
#     references the undecorated `mReg<Name>` names, which are resolved instead by a
#     dedicated undecorated import library built from this same `.def`
#     (`m_mwin32_alias_import.lib`; see DESIGN-NOTES D8 and the mwin32 CMakeLists).
#     The alias declares them with a uniform signature-free
#     `extern "C" void mReg<Name>();` declaration.
#   * The IAT slot is pointer-sized; the client casts through its own declared
#     signature at the call site, so the uniform `void(*)()` slot type is
#     sufficient and keeps this generator free of per-function signatures (which
#     the .def does not carry).
#   * `&mReg<Name>` is a link-time address constant, so the slot is initialized at
#     load time (before any client call), not by a dynamic initializer of
#     unspecified order.
#   * Defining `__imp_Reg<Name>` ourselves satisfies the symbol the client's
#     dllimport call needs, so advapi32's member for that name is never pulled and
#     there is no duplicate-symbol conflict. This is the decisive mechanism.
#   * The `/alternatename` is a weak fallback for a plain (non-dllimport) client
#     reference; it loses to advapi32 when advapi32 is linked, so it is only
#     emitted because it is harmless and helps the no-advapi32 case. Real
#     <windows.h> clients always hit the `__imp_` slot.
#
# x64 only — `__imp_<Name>` is the undecorated x64 spelling. Changing mwin32.def
# regenerates this file; the emitted symbol contract is a breaking change for
# clients linking mwin32_alias.

if(NOT DEFINED MWIN32_DEF)
    message(FATAL_ERROR "generate_mwin32_alias: MWIN32_DEF must be defined")
endif()
if(NOT DEFINED MWIN32_ALIAS_OUT)
    message(FATAL_ERROR "generate_mwin32_alias: MWIN32_ALIAS_OUT must be defined")
endif()
if(NOT EXISTS "${MWIN32_DEF}")
    message(FATAL_ERROR "generate_mwin32_alias: input def not found: ${MWIN32_DEF}")
endif()

file(STRINGS "${MWIN32_DEF}" def_lines)

set(seen_names "")
set(alias_body "")

foreach(raw_line IN LISTS def_lines)
    # Separate any ';' comment (the .def comment marker) from the name so the
    # name validation and the alias-opt-out marker can be inspected separately.
    string(REGEX REPLACE ";.*$" "" name_part "${raw_line}")
    string(REGEX MATCH ";.*$" comment_part "${raw_line}")
    string(STRIP "${name_part}" shim_name)
    if(shim_name STREQUAL "" OR shim_name STREQUAL "EXPORTS")
        continue()
    endif()
    # Every export must be a shim name: 'm' followed by an uppercase letter, or
    # 'm' followed by an underscore for the dusty-deck legacy primitives whose
    # genuine names start with an underscore (`_lopen`, `_lcreat`, `_lread`,
    # `_lclose`, ...). The Win32 name is still the mechanical strip of the
    # leading 'm' (`m_lopen` -> `_lopen`), so the redirect emission is unchanged.
    if(NOT shim_name MATCHES "^m([A-Z]|_)")
        message(FATAL_ERROR
            "generate_mwin32_alias: export '${shim_name}' does not match the m<Name> shim shape")
    endif()
    # The .def may list a name more than once; emit each unique slot exactly once
    # so the alias TU has no duplicate __imp_ definitions.
    if(shim_name IN_LIST seen_names)
        continue()
    endif()
    list(APPEND seen_names "${shim_name}")
    # A 'noalias' marker in the trailing comment means the name is exported by the
    # DLL but is deliberately NOT auto-redirected (opt-in aliasing). Skip it here;
    # it still participates in the DLL exports and the import library.
    if(comment_part MATCHES "noalias")
        continue()
    endif()
    # Win32 name = shim name with the leading 'm' removed.
    string(SUBSTRING "${shim_name}" 1 -1 win32_name)
    string(APPEND alias_body
        "extern \"C\" void ${shim_name}();\n")
    string(APPEND alias_body
        "extern \"C\" void (*__imp_${win32_name})() = &${shim_name};\n")
    string(APPEND alias_body
        "#pragma comment(linker, \"/alternatename:${win32_name}=${shim_name}\")\n\n")
endforeach()

list(LENGTH seen_names alias_count)

set(alias_header
"// Copyright (c) Microsoft Corporation.
//
// GENERATED FILE - DO NOT EDIT.
// Produced by generate_mwin32_alias.cmake from mwin32.def.
//
// Link this object into a client to redirect its genuine Win32 registry calls to
// the mwin32 shim. ${alias_count} functions are redirected. See DESIGN-NOTES D8.
//
// Each entry defines the __imp_ IAT slot the client's <windows.h> dllimport call
// goes through (the decisive redirect) and a /alternatename fallback for plain
// references. The uniform void(*)() slot type is intentional: the slot is only a
// pointer value; the client casts through its own signature at the call site.

")

file(WRITE "${MWIN32_ALIAS_OUT}" "${alias_header}${alias_body}")

message(STATUS
    "generate_mwin32_alias: wrote ${alias_count} aliases to ${MWIN32_ALIAS_OUT}")

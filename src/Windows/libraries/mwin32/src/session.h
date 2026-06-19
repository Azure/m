// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <memory>

#undef NOMINMAX
#define NOMINMAX

#include <Windows.h>

namespace m::pil
{
    struct ikey;
    struct ifilesystem;
    struct iplatform;
    struct iwebcore;
    struct iwebcore_instance;
} // namespace m::pil

namespace m::mwin32_impl
{
    struct pilcfg;

    //
    // Build the PIL platform a session would run against for a given parsed
    // configuration. When `cfg.persisted_state` is non-empty the result is a
    // snapshot platform loaded from that file (mode (c)) and the layer flags
    // and redirections are ignored; otherwise the layered live platform is
    // built from the flags and redirections. When `cfg.fault_script` is
    // non-empty the fault-injecting layer is wrapped around the selected base
    // stack (best-effort: a missing or malformed fault script leaves the base
    // unwrapped). Exposed (separately from the process-wide session singleton)
    // so the selection logic can be tested without depending on the host
    // module's sidecar file.
    //
    std::shared_ptr<m::pil::iplatform>
    build_platform_from_config(pilcfg const& cfg);

    //
    // Is this raw handle value one of the Win32 predefined registry
    // pseudo-handles (HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER, ...)?
    //
    // The predefined HKEY constants are fixed values in the 0x8000'0000 range
    // and never overlap the handle values this shim mints (see handle_table.h),
    // so a raw value is unambiguously either predefined or a real interned
    // handle.
    //
    bool
    is_predefined_handle_value(std::uintptr_t value) noexcept;

    //
    // If `value` names a predefined registry pseudo-handle, return the backing
    // PIL ikey for it (opened against the active session's registry and cached
    // for the lifetime of the process). Returns nullptr if `value` is not a
    // predefined key.
    //
    std::shared_ptr<m::pil::ikey>
    try_resolve_predefined_ikey(std::uintptr_t value);

    //
    // The filesystem surface (iplatform::get_filesystem) for the active session,
    // opened once against the configured PIL stack and cached for the lifetime
    // of the process. This is the filesystem analogue of the predefined-ikey
    // resolution above: the mFile* / mFind* shims route through it to reach the
    // selected (passthrough / buffered / redirecting / logging / fault) provider.
    //
    std::shared_ptr<m::pil::ifilesystem>
    session_filesystem();

    //
    // The webcore surface (iplatform::get_webcore) for the active session,
    // opened once against the configured PIL stack and cached for the process
    // lifetime. This is the webcore analogue of session_filesystem().
    //
    std::shared_ptr<m::pil::iwebcore>
    session_webcore();

    //
    // Webcore activation lifecycle (D-HWC-5). The session owns at most one
    // activation (iwebcore_instance token); a second activation returns
    // ERROR_SERVICE_ALREADY_RUNNING. These functions manage that single slot:
    //
    //   session_webcore_activate: activates the engine if not already active.
    //       Returns S_OK on success, stores the activation token in the session.
    //       Returns HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING) if already
    //       activated (either by a prior call or by the engine's own contract).
    //       Returns any other failure HRESULT from the engine on error.
    //
    //   session_webcore_shutdown: shuts down the active instance.
    //       Returns S_OK on success.
    //       Returns HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE) if no activation.
    //
    //   session_webcore_set_metadata: forwards metadata to the engine.
    //       Returns S_OK on success, or any failure HRESULT from the engine.
    //
    HRESULT
    session_webcore_activate(PCWSTR pszAppHostConfigFile,
                             PCWSTR pszRootWebConfigFile,
                             PCWSTR pszInstanceName);

    HRESULT
    session_webcore_shutdown(DWORD fImmediate);

    HRESULT
    session_webcore_set_metadata(PCWSTR pszMetadataType, PCWSTR pszValue);

} // namespace m::mwin32_impl

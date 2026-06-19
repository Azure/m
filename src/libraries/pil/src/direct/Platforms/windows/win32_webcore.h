// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <system_error>

#include <m/pil/webcore_interfaces.h>

// Windows headers
#undef NOMINMAX
#define NOMINMAX
#include <Windows.h>

//
// Direct Windows HWC (Hostable Web Core) provider. Loads hwebcore.dll via
// LoadLibraryExW and binds the three entry points (WebCoreActivate,
// WebCoreShutdown, WebCoreSetMetadata) via GetProcAddress. The engine path is
// resolved via GetSystemDirectoryW + "\inetsrv\hwebcore.dll" (D-HWC-3).
//
// The module is loaded once on the first activate call and unloaded on provider
// destruction.
//
// The function-pointer seam (`webcore_engine_api`) allows injecting a fake
// engine for testing (D-HWC-3, M-HWC-DIRECT-5).
//

namespace m::pil::impl::win32
{
    //--------------------------------------------------------------------------
    // Engine ABI — function signatures matching <hwebcore.h>
    //--------------------------------------------------------------------------

    // WebCoreActivate(PCWSTR appHostConfigPath, PCWSTR rootWebConfigPath, PCWSTR instanceName)
    using PFN_WEB_CORE_ACTIVATE = long(__stdcall*)(wchar_t const*, wchar_t const*, wchar_t const*);

    // WebCoreShutdown(DWORD fImmediate)
    using PFN_WEB_CORE_SHUTDOWN = long(__stdcall*)(std::uint32_t);

    // WebCoreSetMetadata(PCWSTR metadataType, PCWSTR metadataValue)
    using PFN_WEB_CORE_SET_METADATA = long(__stdcall*)(wchar_t const*, wchar_t const*);

    //--------------------------------------------------------------------------
    // webcore_engine_api — injectable function-pointer seam
    //--------------------------------------------------------------------------

    struct webcore_engine_api
    {
        PFN_WEB_CORE_ACTIVATE     pfn_activate     = nullptr;
        PFN_WEB_CORE_SHUTDOWN     pfn_shutdown     = nullptr;
        PFN_WEB_CORE_SET_METADATA pfn_set_metadata = nullptr;
    };

    //--------------------------------------------------------------------------
    // webcore_instance — RAII token representing a running activation
    //--------------------------------------------------------------------------

    class webcore_instance final : public iwebcore_instance
    {
    public:
        webcore_instance()                                  = delete;
        webcore_instance(webcore_instance const&)           = delete;
        webcore_instance(webcore_instance&&)                = delete;
        webcore_instance& operator=(webcore_instance const&) = delete;
        webcore_instance& operator=(webcore_instance&&)      = delete;

        // Constructs the RAII token; the engine is already activated.
        webcore_instance(PFN_WEB_CORE_SHUTDOWN pfn_shutdown, bool immediate_shutdown);

        ~webcore_instance() override;

    private:
        PFN_WEB_CORE_SHUTDOWN m_pfn_shutdown;
        bool                  m_immediate_shutdown;
    };

    //--------------------------------------------------------------------------
    // webcore — the live Windows HWC provider
    //--------------------------------------------------------------------------

    class webcore final : public iwebcore, public std::enable_shared_from_this<webcore>
    {
    public:
        // Default construction: bind to the live hwebcore.dll on first activate.
        webcore();

        // Injectable seam: use the provided engine API (for testing).
        explicit webcore(webcore_engine_api api);

        webcore(webcore const&)           = delete;
        webcore(webcore&&)                = delete;
        webcore& operator=(webcore const&) = delete;
        webcore& operator=(webcore&&)      = delete;

        ~webcore() override;

        // iwebcore interface

        activate_disposition
        activate(activate_flags                      flags,
                 activation_request const&           request,
                 std::unique_ptr<iwebcore_instance>& returned_instance,
                 std::error_code&                    ec) override;

        set_metadata_disposition
        set_metadata(set_metadata_flags  flags,
                     std::u16string_view type,
                     std::u16string_view value,
                     std::error_code&    ec) override;

    private:
        // Ensures the engine is loaded (m_api bound). Returns false on failure,
        // setting ec to the load error.
        bool
        ensure_engine_loaded(std::error_code& ec);

        std::mutex         m_mutex;
        webcore_engine_api m_api;                    // function pointers
        HMODULE            m_module{nullptr};        // DLL handle (null if injected seam)
        bool               m_has_active_instance{false}; // single-activation enforcement
        bool               m_use_injected_api{false};    // true if ctor was given an API
    };

} // namespace m::pil::impl::win32

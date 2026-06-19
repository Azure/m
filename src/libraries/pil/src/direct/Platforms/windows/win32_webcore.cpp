// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "win32_webcore.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>
#include <utility>

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/pil/file_path.h>
#include <m/strings/convert.h>

// Windows headers
#undef NOMINMAX
#define NOMINMAX
#include <Windows.h>
#include <winerror.h>

namespace
{
    // HRESULT indicating already-activated (the HWC single-activation contract).
    constexpr long k_already_running = static_cast<long>(0x80070420); // HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING)

    // HRESULT indicating service not active (on shutdown with no activation).
    constexpr long k_not_active = static_cast<long>(0x80070426); // HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE)

    // S_OK
    constexpr long k_ok = 0L;

    // Convert an HRESULT to std::error_code.
    std::error_code
    hresult_to_ec(long hr)
    {
        if (hr >= 0)
            return {};
        // HRESULT is already in the form we need for system_category on Windows.
        // The low 16 bits are the Win32 error code; for FACILITY_WIN32 HRESULTs,
        // we extract the underlying code.
        if ((hr & 0xFFFF0000) == 0x80070000)
        {
            // FACILITY_WIN32: extract the lower 16 bits as the Win32 error code.
            auto const win32_code = hr & 0x0000FFFF;
            return std::error_code(static_cast<int>(win32_code), std::system_category());
        }
        // Other facilities: use the HRESULT as-is.
        return std::error_code(static_cast<int>(hr), std::system_category());
    }

    // Build the absolute path to hwebcore.dll: %SystemRoot%\System32\inetsrv\hwebcore.dll
    std::wstring
    get_hwebcore_path()
    {
        wchar_t system_dir[MAX_PATH + 1] = {};
        auto const len = ::GetSystemDirectoryW(system_dir, static_cast<UINT>(std::size(system_dir)));
        if (len == 0 || len >= std::size(system_dir))
        {
            // Fall back to a hard-coded path (extremely unlikely).
            return L"C:\\Windows\\System32\\inetsrv\\hwebcore.dll";
        }
        std::wstring path(system_dir, len);
        path += L"\\inetsrv\\hwebcore.dll";
        return path;
    }

} // namespace

namespace m::pil::impl::win32
{
    //--------------------------------------------------------------------------
    // webcore_instance
    //--------------------------------------------------------------------------

    webcore_instance::webcore_instance(PFN_WEB_CORE_SHUTDOWN pfn_shutdown, bool immediate_shutdown):
        m_pfn_shutdown(pfn_shutdown),
        m_immediate_shutdown(immediate_shutdown)
    {
        M_INTERNAL_ERROR_CHECK(m_pfn_shutdown != nullptr);
    }

    webcore_instance::~webcore_instance()
    {
        if (m_pfn_shutdown)
        {
            // fImmediate: 0 = graceful, 1 = immediate
            std::uint32_t const f_immediate = m_immediate_shutdown ? 1u : 0u;
            auto const hr = m_pfn_shutdown(f_immediate);
            // Ignore ERROR_SERVICE_NOT_ACTIVE — means the engine already shut down.
            if (hr != k_ok && hr != k_not_active)
            {
                // Can't throw from destructor; just swallow the error.
            }
        }
    }

    //--------------------------------------------------------------------------
    // webcore
    //--------------------------------------------------------------------------

    webcore::webcore(): m_api{}, m_module{nullptr}, m_has_active_instance{false}, m_use_injected_api{false}
    {}

    webcore::webcore(webcore_engine_api api):
        m_api{api},
        m_module{nullptr},
        m_has_active_instance{false},
        m_use_injected_api{true}
    {}

    webcore::~webcore()
    {
        if (m_module != nullptr)
        {
            ::FreeLibrary(m_module);
            m_module = nullptr;
        }
    }

    bool
    webcore::ensure_engine_loaded(std::error_code& ec)
    {
        // Already loaded (or using injected API)?
        if (m_api.pfn_activate != nullptr)
            return true;

        // If we're using an injected seam but it's null, that's a test misconfiguration.
        if (m_use_injected_api)
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return false;
        }

        // Load the module from the absolute path.
        std::wstring const path = get_hwebcore_path();

        // Use LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR so that the engine's sibling DLLs
        // (iisutil.dll, etc.) resolve from the same directory. The bare
        // LOAD_LIBRARY_SEARCH_SYSTEM32 fails with ERROR_MOD_NOT_FOUND because
        // system32 doesn't contain the inetsrv siblings.
        HMODULE const h_module = ::LoadLibraryExW(
            path.c_str(),
            nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);

        if (!h_module)
        {
            ec = hresult_to_ec(HRESULT_FROM_WIN32(::GetLastError()));
            return false;
        }

        m_module = h_module;

        // Resolve the three entry points.
        m_api.pfn_activate = reinterpret_cast<PFN_WEB_CORE_ACTIVATE>(
            ::GetProcAddress(h_module, "WebCoreActivate"));
        m_api.pfn_shutdown = reinterpret_cast<PFN_WEB_CORE_SHUTDOWN>(
            ::GetProcAddress(h_module, "WebCoreShutdown"));
        m_api.pfn_set_metadata = reinterpret_cast<PFN_WEB_CORE_SET_METADATA>(
            ::GetProcAddress(h_module, "WebCoreSetMetadata"));

        if (!m_api.pfn_activate || !m_api.pfn_shutdown || !m_api.pfn_set_metadata)
        {
            ec = hresult_to_ec(HRESULT_FROM_WIN32(::GetLastError()));
            ::FreeLibrary(m_module);
            m_module = nullptr;
            m_api    = {};
            return false;
        }

        return true;
    }

    iwebcore::activate_disposition
    webcore::activate(activate_flags                      flags,
                      activation_request const&           request,
                      std::unique_ptr<iwebcore_instance>& returned_instance,
                      std::error_code&                    ec)
    {
        ec.clear();
        returned_instance.reset();

        std::lock_guard<std::mutex> guard(m_mutex);

        // Single-activation enforcement (D-HWC-5): if we already have an
        // instance, return `already_activated` without calling the engine.
        if (m_has_active_instance)
        {
            return activate_disposition{activate_result_code::already_activated};
        }

        // Ensure engine is loaded.
        if (!ensure_engine_loaded(ec))
            return {};

        // Convert file_path values to null-terminated wide strings.
        // file_path::c_str() returns a null-terminated char16_t*, which we
        // reinterpret as wchar_t* (both are 16-bit on Windows).
        wchar_t const* app_host_config_ptr =
            reinterpret_cast<wchar_t const*>(request.app_host_config.c_str());

        wchar_t const* root_web_config_ptr = nullptr;
        if (request.root_web_config)
        {
            root_web_config_ptr =
                reinterpret_cast<wchar_t const*>(request.root_web_config->c_str());
        }

        // instance_name is a u16string — we need to null-terminate it.
        std::wstring const instance_name_str(
            reinterpret_cast<wchar_t const*>(request.instance_name.data()),
            request.instance_name.size());

        // Call the engine.
        auto const hr = m_api.pfn_activate(
            app_host_config_ptr,
            root_web_config_ptr,
            instance_name_str.c_str());

        if (hr == k_already_running)
        {
            // The engine itself reported already-running. This shouldn't happen
            // because we track m_has_active_instance, but the engine is authoritative.
            return activate_disposition{activate_result_code::already_activated};
        }

        if (hr != k_ok)
        {
            ec = hresult_to_ec(hr);
            return {};
        }

        // Success: create the RAII token.
        bool const immediate_shutdown =
            (flags & activate_flags::immediate_shutdown_on_release) != activate_flags{};

        returned_instance =
            std::make_unique<webcore_instance>(m_api.pfn_shutdown, immediate_shutdown);

        m_has_active_instance = true;

        // Note: m_has_active_instance should be cleared when the instance is
        // destroyed. For now, we rely on the caller to not destroy the instance
        // and then try to activate again without a fresh webcore provider.
        // TODO: Hook the instance destructor to call back and clear the flag.

        return {};
    }

    iwebcore::set_metadata_disposition
    webcore::set_metadata(set_metadata_flags  flags,
                          std::u16string_view type,
                          std::u16string_view value,
                          std::error_code&    ec)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, set_metadata_flags{});
        ec.clear();

        std::lock_guard<std::mutex> guard(m_mutex);

        // Engine must be loaded (can only set metadata while active).
        if (!m_api.pfn_set_metadata)
        {
            ec = std::make_error_code(std::errc::invalid_argument);
            return {};
        }

        // Convert to null-terminated wide strings.
        std::wstring const type_str(reinterpret_cast<wchar_t const*>(type.data()), type.size());
        std::wstring const value_str(reinterpret_cast<wchar_t const*>(value.data()), value.size());

        auto const hr = m_api.pfn_set_metadata(type_str.c_str(), value_str.c_str());
        if (hr != k_ok)
        {
            ec = hresult_to_ec(hr);
            return {};
        }

        return {};
    }

} // namespace m::pil::impl::win32

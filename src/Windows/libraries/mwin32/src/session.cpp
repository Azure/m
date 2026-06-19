// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <filesystem>

#include <m/pil/fault.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/webcore_interfaces.h>

#include "pilcfg.h"
#include "session.h"
#include "webcore_config_platform.h"
#include "win32_error_mapping.h"

namespace m::mwin32_impl
{
    namespace
    {
        //
        // Translate a parsed .pilcfg into the PIL stack-selection flags. An
        // all-false config maps to no flags, i.e. passthrough.
        //
        m::pil::make_platform_flags
        to_platform_flags(pilcfg const& cfg) noexcept
        {
            auto flags = m::pil::make_platform_flags{};

            if (cfg.buffer_updates)
                flags |= m::pil::make_platform_flags::buffer_updates;

            if (cfg.record_modifications)
                flags |= m::pil::make_platform_flags::record_modifications;

            return flags;
        }

        //
        // Map a raw handle value to the predefined registry key it names, if
        // any. The values mirror the Win32 HKEY_* constants exactly (see
        // m::pil::predefined_key).
        //
        // The Win32 predefined HKEY constants are defined as
        // (HKEY)(ULONG_PTR)(LONG)0x8000'000N: a 32-bit value with bit 31 set,
        // sign-extended to pointer width. On 64-bit that yields a handle value
        // of 0xFFFF'FFFF'8000'000N, so we must recover the low 32 bits (and
        // confirm the upper bits are exactly that sign-extension) before
        // comparing against the enum. Interned table handles are minted as
        // small positive values, so they never collide with these.
        //
        std::optional<m::pil::predefined_key>
        map_value_to_predefined_key(std::uintptr_t value) noexcept
        {
            auto const sign_extended =
                static_cast<std::uintptr_t>(static_cast<std::intptr_t>(
                    static_cast<std::int32_t>(static_cast<std::uint32_t>(value))));
            if (sign_extended != value)
                return std::nullopt;

            switch (static_cast<std::uint32_t>(value))
            {
                case static_cast<std::uint32_t>(m::pil::predefined_key::classes_root):
                case static_cast<std::uint32_t>(m::pil::predefined_key::current_user):
                case static_cast<std::uint32_t>(m::pil::predefined_key::local_machine):
                case static_cast<std::uint32_t>(m::pil::predefined_key::users):
                case static_cast<std::uint32_t>(m::pil::predefined_key::performance_data):
                case static_cast<std::uint32_t>(m::pil::predefined_key::current_config):
                case static_cast<std::uint32_t>(
                    m::pil::predefined_key::current_user_local_settings):
                case static_cast<std::uint32_t>(m::pil::predefined_key::performance_text):
                case static_cast<std::uint32_t>(m::pil::predefined_key::performance_nlstext):
                    return static_cast<m::pil::predefined_key>(static_cast<std::uint32_t>(value));
                default:
                    return std::nullopt;
            }
        }

        //
        // The process-wide PIL session backing the mReg* shim. Lazily created
        // on first use. The default configuration is passthrough to the live
        // Win32 registry; a future `.pilcfg` sidecar will be able to select a
        // logging or buffered stack instead.
        //
        class session
        {
        public:
            static session&
            instance()
            {
                static session s;
                return s;
            }

            std::shared_ptr<m::pil::ikey>
            predefined_ikey(m::pil::predefined_key pk)
            {
                auto l = std::unique_lock(m_mutex);

                auto it = m_predefined_cache.find(pk);
                if (it != m_predefined_cache.end())
                    return it->second;

                auto ikey = m_registry->open_predefined_key(pk);
                m_predefined_cache.emplace(pk, ikey);
                return ikey;
            }

            //
            // The filesystem surface for this session, opened lazily against the
            // configured platform stack and cached for the process lifetime
            // (mirrors the predefined-ikey cache above).
            //
            std::shared_ptr<m::pil::ifilesystem>
            filesystem()
            {
                auto l = std::unique_lock(m_mutex);

                if (!m_filesystem)
                    m_filesystem = m_platform->get_filesystem();

                return m_filesystem;
            }

            //
            // The webcore surface for this session, opened lazily against the
            // configured platform stack and cached for the process lifetime.
            //
            std::shared_ptr<m::pil::iwebcore>
            webcore()
            {
                auto l = std::unique_lock(m_mutex);

                if (!m_webcore)
                    m_webcore = m_platform->get_webcore();

                return m_webcore;
            }

            //
            // Webcore activation lifecycle (D-HWC-5). The session owns at most
            // one iwebcore_instance; a second activation returns the
            // ERROR_SERVICE_ALREADY_RUNNING HRESULT.
            //
            HRESULT
            webcore_activate(PCWSTR pszAppHostConfigFile,
                             PCWSTR pszRootWebConfigFile,
                             PCWSTR pszInstanceName)
            {
                auto l = std::unique_lock(m_mutex);

                // Single-activation-per-process contract: if we already hold an
                // instance, return the already-running HRESULT immediately.
                if (m_webcore_instance)
                    return HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING);

                if (!m_webcore)
                    m_webcore = m_platform->get_webcore();

                // Build the activation request from the raw PCWSTR arguments.
                // On Windows, wchar_t and char16_t are the same encoding (UTF-16),
                // so we reinterpret_cast the PCWSTR strings.
                m::pil::activation_request req;
                req.app_host_config = m::pil::file_path(pszAppHostConfigFile ? pszAppHostConfigFile : L"");
                if (pszRootWebConfigFile && pszRootWebConfigFile[0] != L'\0')
                    req.root_web_config = m::pil::file_path(pszRootWebConfigFile);
                req.instance_name = pszInstanceName
                    ? std::u16string(reinterpret_cast<const char16_t*>(pszInstanceName))
                    : std::u16string();

                // Attempt activation through the webcore surface.
                std::unique_ptr<m::pil::iwebcore_instance> instance;
                std::error_code ec;
                auto const disposition = m_webcore->activate(
                    m::pil::iwebcore::activate_flags{}, req, instance, ec);

                if (ec)
                    return HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()));

                // Check if the engine reported already_activated (its own
                // contract, separate from our session-level check above).
                if (disposition.code() == m::pil::iwebcore::activate_result_code::already_activated)
                    return HRESULT_FROM_WIN32(ERROR_SERVICE_ALREADY_RUNNING);

                m_webcore_instance = std::move(instance);
                return S_OK;
            }

            HRESULT
            webcore_shutdown(DWORD fImmediate)
            {
                auto l = std::unique_lock(m_mutex);

                // No active instance → ERROR_SERVICE_NOT_ACTIVE.
                if (!m_webcore_instance)
                    return HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE);

                // Destroy the instance token; the provider's destructor handles
                // the actual shutdown. TODO: if fImmediate matters for the
                // destructor's behavior, consider storing it or passing it to a
                // different shutdown method. For now the RAII token handles it.
                (void)fImmediate;
                m_webcore_instance.reset();
                return S_OK;
            }

            HRESULT
            webcore_set_metadata(PCWSTR pszMetadataType, PCWSTR pszValue)
            {
                auto l = std::unique_lock(m_mutex);

                if (!m_webcore)
                    m_webcore = m_platform->get_webcore();

                std::u16string_view type(reinterpret_cast<const char16_t*>(pszMetadataType),
                                         pszMetadataType ? wcslen(pszMetadataType) : 0);
                std::u16string_view value(reinterpret_cast<const char16_t*>(pszValue),
                                          pszValue ? wcslen(pszValue) : 0);

                std::error_code ec;
                m_webcore->set_metadata(m::pil::iwebcore::set_metadata_flags{}, type, value, ec);

                if (ec)
                    return HRESULT_FROM_WIN32(static_cast<DWORD>(ec.value()));

                return S_OK;
            }

        private:
            session(): session(load_pilcfg()) {}

            explicit session(pilcfg cfg):
                m_capture_snapshot(cfg.capture_snapshot),
                m_diagnostic_log(cfg.diagnostic_log),
                m_platform(build_platform_from_config(cfg)),
                m_registry(m_platform->get_registry())
            {}

            ~session()
            {
                // Best-effort capture: if a snapshot path was configured, persist
                // the session's registry state on process exit. A failure to write
                // must never throw from this destructor (which runs during static
                // teardown), so all errors are swallowed.
                if (!m_capture_snapshot.empty())
                {
                    try
                    {
                        m::pil::platform(std::shared_ptr<m::pil::iplatform>(m_platform))
                            .save(std::filesystem::path(m_capture_snapshot));
                    }
                    catch (...)
                    {
                    }
                }

                // Best-effort diagnostic log: if a log path was configured, emit
                // the ordered modification trace on process exit. Same no-throw
                // discipline as the snapshot save above.
                if (!m_diagnostic_log.empty())
                {
                    try
                    {
                        m::pil::platform(std::shared_ptr<m::pil::iplatform>(m_platform))
                            .save_diagnostic_log(std::filesystem::path(m_diagnostic_log));
                    }
                    catch (...)
                    {
                    }
                }
            }

            std::u16string                                                 m_capture_snapshot;
            std::u16string                                                 m_diagnostic_log;
            std::mutex                                                     m_mutex;
            std::shared_ptr<m::pil::iplatform>                             m_platform;
            std::shared_ptr<m::pil::iregistry>                             m_registry;
            std::shared_ptr<m::pil::ifilesystem>                           m_filesystem;
            std::shared_ptr<m::pil::iwebcore>                              m_webcore;
            std::unique_ptr<m::pil::iwebcore_instance>                     m_webcore_instance;
            std::map<m::pil::predefined_key, std::shared_ptr<m::pil::ikey>> m_predefined_cache;
        };
    } // namespace

    std::shared_ptr<m::pil::iplatform>
    build_platform_from_config(pilcfg const& cfg)
    {
        // Build the base stack: a persisted snapshot (mode (c)) ignores the
        // layer flags and redirections; otherwise the layered live platform.
        std::shared_ptr<m::pil::iplatform> base;
        if (!cfg.persisted_state.empty())
        {
            base = m::pil::load_platform_interface(
                std::filesystem::path(cfg.persisted_state));
        }
        else
        {
            std::vector<std::pair<std::u16string_view, std::u16string_view>> redirection_views;
            redirection_views.reserve(cfg.redirections.size());
            for (auto const& r: cfg.redirections)
                redirection_views.emplace_back(r.first, r.second);

            base = m::pil::make_platform_interface(to_platform_flags(cfg), redirection_views);
        }

        // Layer the fault-injecting platform on top of whatever base was
        // selected. Loading the referenced fault script is best-effort: a
        // missing or malformed script leaves the base stack unwrapped rather
        // than breaking the host (tolerant load, per D5/D7).
        if (!cfg.fault_script.empty())
        {
            try
            {
                auto script = m::pil::load_fault_script(
                    std::filesystem::path(cfg.fault_script));
                base = m::pil::apply_fault_layer(base, script);
            }
            catch (...)
            {
            }
        }

        // Layer the webcore-configuring platform if webcore config is present.
        // This wraps the webcore surface with the requested configuration
        // (interception mode, endpoints, materialization_dir, fault_script).
        // Like the fault layer, this is applied as a platform decorator.
        if (cfg.webcore.has_value())
        {
            base = apply_webcore_config(base, cfg.webcore.value());
        }

        return base;
    }

    bool
    is_predefined_handle_value(std::uintptr_t value) noexcept
    {
        return map_value_to_predefined_key(value).has_value();
    }

    std::shared_ptr<m::pil::ikey>
    try_resolve_predefined_ikey(std::uintptr_t value)
    {
        auto pk = map_value_to_predefined_key(value);
        if (!pk.has_value())
            return nullptr;

        return session::instance().predefined_ikey(pk.value());
    }

    std::shared_ptr<m::pil::ifilesystem>
    session_filesystem()
    {
        return session::instance().filesystem();
    }

    std::shared_ptr<m::pil::iwebcore>
    session_webcore()
    {
        return session::instance().webcore();
    }

    HRESULT
    session_webcore_activate(PCWSTR pszAppHostConfigFile,
                             PCWSTR pszRootWebConfigFile,
                             PCWSTR pszInstanceName)
    {
        try
        {
            return session::instance().webcore_activate(
                pszAppHostConfigFile, pszRootWebConfigFile, pszInstanceName);
        }
        catch (...)
        {
            return map_pil_exception_to_hresult();
        }
    }

    HRESULT
    session_webcore_shutdown(DWORD fImmediate)
    {
        try
        {
            return session::instance().webcore_shutdown(fImmediate);
        }
        catch (...)
        {
            return map_pil_exception_to_hresult();
        }
    }

    HRESULT
    session_webcore_set_metadata(PCWSTR pszMetadataType, PCWSTR pszValue)
    {
        try
        {
            return session::instance().webcore_set_metadata(pszMetadataType, pszValue);
        }
        catch (...)
        {
            return map_pil_exception_to_hresult();
        }
    }

} // namespace m::mwin32_impl

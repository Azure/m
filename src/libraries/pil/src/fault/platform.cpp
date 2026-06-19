// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>

#include <m/error_handling/macros.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>

#include "fault.h"

namespace m::pil::impl::fault
{
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const&    underlying_platform,
                    std::shared_ptr<fault_script> const& script)
    {
        return std::make_shared<platform>(underlying_platform, script);
    }

    platform::platform(std::shared_ptr<iplatform> const&    underlying_platform,
                       std::shared_ptr<fault_script> const& script):
        m_underlying_platform(underlying_platform),
        m_script(script),
        m_registry{std::make_shared<registry>(m_underlying_platform->get_registry(), m_script)},
        m_filesystem{
            std::make_shared<filesystem>(m_underlying_platform->get_filesystem(), m_script)}
    {
        M_INTERNAL_ERROR_CHECK(m_script.get() != nullptr);
    }

    iplatform::get_registry_disposition
    platform::get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry)
    {
        returned_registry.reset();

        if (flags != get_registry_flags{})
            throw std::runtime_error("iplatform::get_registry() called with invalid flags");

        returned_registry = m_registry;

        return get_registry_disposition{};
    }

    iplatform::get_filesystem_disposition
    platform::get_filesystem(get_filesystem_flags          flags,
                             std::shared_ptr<ifilesystem>& returned_filesystem)
    {
        returned_filesystem.reset();

        if (flags != get_filesystem_flags{})
            throw std::runtime_error("iplatform::get_filesystem() called with invalid flags");

        returned_filesystem = m_filesystem;

        return get_filesystem_disposition{};
    }

    iplatform::get_webcore_disposition
    platform::get_webcore(get_webcore_flags          flags,
                          std::shared_ptr<iwebcore>& returned_webcore)
    {
        returned_webcore.reset();

        if (flags != get_webcore_flags{})
            throw std::runtime_error("iplatform::get_webcore() called with invalid flags");

        if (!m_webcore)
        {
            std::shared_ptr<iwebcore> underlying_webcore;
            auto d = m_underlying_platform->get_webcore(flags, underlying_webcore);
            (void)d;
            if (underlying_webcore)
                m_webcore = std::make_shared<webcore>(underlying_webcore, m_script);
        }

        returned_webcore = m_webcore;

        return get_webcore_disposition{};
    }

    iplatform::get_http_contract_disposition
    platform::get_http_contract(get_http_contract_flags          flags,
                                std::shared_ptr<ihttp_contract>& returned_http_contract)
    {
        // Contracts are pure spec validators, independent of engine liveness;
        // forward to the underlying platform's provider (mirrors get_webcore).
        return m_underlying_platform->get_http_contract(flags, returned_http_contract);
    }

    iplatform::save_disposition
    platform::save(save_flags flags, save_contents contents, pugi::xml_node& platform_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // The fault script is a separate input artifact, never folded into the
        // persisted <Platform>. Persistence is a transparent pass-through.
        return m_underlying_platform->save(flags, contents, platform_element);
    }

    iplatform::save_disposition
    platform::save_diagnostic_log(save_flags flags, pugi::xml_node& diagnostic_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // The fault layer records no diagnostic trace of its own; forward so a
        // logging tap placed below remains reachable from the top.
        if (m_underlying_platform)
            return m_underlying_platform->save_diagnostic_log(flags, diagnostic_element);

        return save_disposition{};
    }
} // namespace m::pil::impl::fault

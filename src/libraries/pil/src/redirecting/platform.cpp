// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "redirecting.h"

namespace m::pil::impl::redirecting
{
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const&                underlying_platform,
                    std::span<std::pair<view_type, view_type> const> registry_redirections)
    {
        return std::make_shared<platform>(underlying_platform, registry_redirections);
    }

    platform::platform(
        std::shared_ptr<iplatform> const&                underlying_platform,
        std::span<std::pair<view_type, view_type> const> registry_redirections,
        std::span<std::pair<view_type, view_type> const> filesystem_redirections):
        m_underlying_platform(underlying_platform),
        m_registry{std::make_shared<registry>(m_underlying_platform->get_registry(),
                                              registry_redirections)},
        m_fs_redirector{std::make_shared<fs_redirector>(filesystem_redirections)},
        m_filesystem{std::make_shared<filesystem>(m_underlying_platform->get_filesystem(),
                                                  m_fs_redirector)}
    {}

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
                m_webcore = std::make_shared<webcore>(underlying_webcore, m_fs_redirector);
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

        // we don't save anything today, just pass through.

        return m_underlying_platform->save(flags, contents, platform_element);
    }

    iplatform::save_disposition
    platform::save_diagnostic_log(save_flags flags, pugi::xml_node& diagnostic_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // D6: forward to lower layers so a logging tap beneath the redirecting
        // layer is reachable from the top.
        return m_underlying_platform->save_diagnostic_log(flags, diagnostic_element);
    }

} // namespace m::pil::impl::redirecting

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

using namespace std::string_view_literals;

#include "logging.h"

namespace m::pil::impl::logging
{
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const& underlying_platform)
    {
        return std::make_shared<platform>(underlying_platform);
    }

    platform::platform(std::shared_ptr<iplatform> const& underlying_platform):
        m_underlying_platform(underlying_platform),
        m_log(std::make_shared<log>()),
        m_registry{std::make_shared<registry>(m_underlying_platform->get_registry(), m_log)},
        m_filesystem{std::make_shared<filesystem>(m_underlying_platform->get_filesystem(), m_log)}
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
                m_webcore = std::make_shared<webcore>(underlying_webcore, m_log);
        }

        returned_webcore = m_webcore;

        return get_webcore_disposition{};
    }

    iplatform::save_disposition
    platform::save(save_flags flags, save_contents contents, pugi::xml_node& platform_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // D6: the diagnostic log is never written into the persisted <Platform>.
        // The logging layer is a transparent pass-through for persistence; the
        // requested-vs-done trace is obtained separately via save_diagnostic_log.
        return m_underlying_platform->save(flags, contents, platform_element);
    }

    iplatform::save_disposition
    platform::save_diagnostic_log(save_flags flags, pugi::xml_node& diagnostic_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});

        // D6: emit the requested-vs-done trace into the caller's side artifact
        // node. This is never reachable from the persisted <Platform>.
        auto log_element = diagnostic_element.append_child(M_PUGIXML_T("Log"sv));

        m_log->save(log_element);

        // The logging tap can float at any depth, and several taps may be
        // stacked. Forward the request down so a deeper tap also contributes its
        // own <Log> section.
        if (m_underlying_platform)
            return m_underlying_platform->save_diagnostic_log(flags, diagnostic_element);

        return save_disposition{};
    }

} // namespace m::pil::impl::logging

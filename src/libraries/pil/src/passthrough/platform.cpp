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

#include "passthrough.h"

namespace m::pil::impl::passthrough
{
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const& underlying_platform)
    {
        return std::make_shared<platform>(underlying_platform);
    }

    platform::platform(std::shared_ptr<iplatform> const& underlying_platform):
        m_underlying_platform(underlying_platform),
        m_registry{std::make_shared<registry>(m_underlying_platform->get_registry())},
        m_filesystem{std::make_shared<filesystem>(m_underlying_platform->get_filesystem())}
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
        // M-HWC-FACETS-1: Passthrough forwards get_webcore to the underlying platform.
        return m_underlying_platform->get_webcore(flags, returned_webcore);
    }

    iplatform::save_disposition
    platform::save(save_flags flags, save_contents contents, pugi::xml_node& platform_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});
        return m_underlying_platform->save(flags, contents, platform_element);
    }

    iplatform::save_disposition
    platform::save_diagnostic_log(save_flags flags, pugi::xml_node& diagnostic_element)
    {
        M_VALIDATE_FLAGS_PARAMETER(flags, save_flags{});
        return m_underlying_platform->save_diagnostic_log(flags, diagnostic_element);
    }

} // namespace m::pil::impl::passthrough

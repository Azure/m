// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/byte_streams/byte_streams.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "buffered.h"

namespace m::pil::impl::buffered
{
    std::shared_ptr<iplatform>
    create_platform(std::shared_ptr<iplatform> const& underlying_platform)
    {
        return std::make_shared<platform>(underlying_platform);
    }

    platform::platform(std::shared_ptr<iplatform> const& underlying_platform):
        m_underlying_platform(underlying_platform)
    {}

    platform::platform(std::shared_ptr<iplatform>&& underlying_platform) noexcept:
        m_underlying_platform(std::move(underlying_platform))
    {}

    platform::platform(platform&& other) noexcept
    {
        using std::swap;

        swap(m_underlying_platform, other.m_underlying_platform);
    }

    platform&
    platform::operator=(platform&& other) noexcept
    {
        using std::swap;

        swap(m_underlying_platform, other.m_underlying_platform);

        return *this;
    }

    iplatform::get_registry_disposition
    platform::get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry)
    {
        returned_registry.reset();

        if (flags != get_registry_flags{})
            throw std::runtime_error("iplatform::get_registry() called with invalid flags");

        returned_registry = std::make_shared<registry>(m_underlying_platform->get_registry());
        return get_registry_disposition{};
    }

    void
    platform::write_to_xml(std::filesystem::path const& path)
    {
        //
    }

    std::shared_ptr<iplatform>
    platform::load_from_xml(std::shared_ptr<iplatform> const& underlying_platform,
                            std::filesystem::path const&      path)
    {
        //
    }

} // namespace m::pil::impl::buffered

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
    create_platform(std::shared_ptr<iplatform> const&                      underlying_platform,
                    std::initializer_list<std::pair<view_type, view_type>> registry_redirections)
    {
        return std::make_shared<platform>(underlying_platform, registry_redirections);
    }

    platform::platform(
        std::shared_ptr<iplatform> const&                      underlying_platform,
        std::initializer_list<std::pair<view_type, view_type>> registry_redirections):
        m_underlying_platform(underlying_platform),
        m_registry{
            std::make_shared<registry>(m_underlying_platform->get_registry(), registry_redirections)}
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

} // namespace m::pil::impl::redirecting

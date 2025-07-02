// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "platform.h"

namespace m::pil
{
    platform
    make_platform(platform_type pt)
    {
        auto sp = impl::create_platform_interface(pt);
        return platform(std::move(sp));
    }

    platform::platform(platform&& other) noexcept
    {
        using std::swap;

        swap(m_platform, other.m_platform);
    }

    platform::platform(std::shared_ptr<iplatform>&& sp) noexcept : m_platform(std::move(sp)){}

    registry platform::get_registry()
    {
        return registry(m_platform->get_registry());
    }


} // namespace m::pil

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <m/pil/platform.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include "common.h"
#include "disposition.h"
#include "security_attributes.h"

#include "registry_base_types.h"
#include "registry_interfaces.h"

namespace m::pil
{
    class platform
    {
    public:
        platform() = default;
        platform(platform&&) noexcept;
        platform(platform const& other);
        platform(std::shared_ptr<iplatform>&&) noexcept;
        ~platform() = default;

        platform&
        operator=(platform&& other);
        platform&
        operator=(platform const& other);

        friend void
        swap(platform& l, platform& r) noexcept
        {
            using std::swap;
            swap(l.m_platform, r.m_platform);
        }

        registry
        get_registry();

    private:
        std::shared_ptr<iplatform> m_platform;
    };
    //
} // namespace m::pil

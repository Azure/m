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
#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/pil/security_attributes.h>
#include <m/pil/registry_base_types.h>
#include <m/pil/registry_interfaces.h>

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
        operator=(platform&& other) noexcept;
        platform&
        operator=(platform const& other);

        void
        swap(platform& other) noexcept;

        registry_class
        get_registry();

    private:
        std::shared_ptr<iplatform> m_platform;
    };
    //
} // namespace m::pil

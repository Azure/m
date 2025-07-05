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
#include <vector>

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

//
// In the platform isolation layer, a "platform" represents a stack of whatever various layers
// are working together to provide a functional mock platform.
//
// The platform object itself provides a minimal interface which only gives access to root
// objects of the various types.
//

namespace m::pil::platform::interfaces
{
    struct platform
    {
        virtual ~platform() {}

        //
        //  get_registry
        //

        enum class get_registry_flags : uint64_t
        {
        };

        enum class get_registry_result_code : uint32_t
        {
        };

        enum class get_registry_result_flags : uint32_t
        {
        };

        using get_registry_disposition =
            disposition<get_registry_result_code, get_registry_result_flags>;

        virtual get_registry_disposition
        get_registry(
            get_registry_flags                                       flags,
            std::shared_ptr<m::pil::registry::interfaces::registry>& returned_registry) = 0;

        std::shared_ptr<m::pil::registry::interfaces::registry>
        get_registry()
        {
            std::shared_ptr<m::pil::registry::interfaces::registry> returned_registry;
            auto const d = get_registry(get_registry_flags{}, returned_registry);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_registry;
        }

        //
    };
} // namespace m::pil::platform::interfaces
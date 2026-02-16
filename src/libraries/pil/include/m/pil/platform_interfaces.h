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

#include <m/error_handling/macros.h>
#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/pil/registry_base_types.h>
#include <m/pil/registry_interfaces.h>
#include <m/pil/security_attributes.h>
#include <m/strings/convert.h>
#include <m/utility/enum_operations.h>
#include <m/utility/utility.h>

#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

#include <pugixml.hpp>

//
// In the platform isolation layer, a "platform" represents a stack of whatever various layers
// are working together to provide a functional mock platform.
//
// The platform object itself provides a minimal interface which only gives access to root
// objects of the various types.
//

namespace m::pil
{
    struct iplatform
    {
        virtual ~iplatform() {}

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
        get_registry(get_registry_flags flags, std::shared_ptr<iregistry>& returned_registry) = 0;

        std::shared_ptr<m::pil::iregistry>
        get_registry()
        {
            std::shared_ptr<iregistry> returned_registry;
            auto const                 d = get_registry(get_registry_flags{}, returned_registry);
            M_INTERNAL_ERROR_CHECK(!d);
            return returned_registry;
        }

        //
        // save
        //

        enum class save_flags : uint64_t
        {
            //
        };

        enum class save_contents
        {
            change_log,
        };

        enum class save_result_code : uint32_t
        {
            //
        };

        enum class save_result_flags : uint32_t
        {
            //
        };

        using save_disposition = disposition<save_result_code, save_result_flags>;

        virtual save_disposition
        save(save_flags flags, save_contents contents, pugi::xml_node& platform_element) = 0;

        void
        save(save_contents contents, pugi::xml_node& platform_element)
        {
            auto const d = save(save_flags{}, contents, platform_element);
            M_INTERNAL_ERROR_CHECK(!d);
        }
    };

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_registry_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::get_registry_result_flags);

    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::save_flags);
    M_DEFINE_SCOPED_ENUM_BITFLAG_OPS(iplatform::save_result_flags);

} // namespace m::pil

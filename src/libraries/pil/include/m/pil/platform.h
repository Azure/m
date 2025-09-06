// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include <m/pil/common.h>
#include <m/pil/disposition.h>
#include <m/pil/platform.h>
#include <m/pil/platform_interfaces.h>
#include <m/pil/registry.h>
#include <m/pil/registry_base_types.h>
#include <m/pil/registry_interfaces.h>
#include <m/pil/security_attributes.h>
#include <m/strings/convert.h>
#include <m/utility/utility.h>

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

        enum class save_format
        {
            xml,
        };

        enum class save_contents
        {
            change_log,
        };

        void
        save(std::filesystem::path const& p,
             save_contents                contents = save_contents::change_log,
             save_format                  format   = save_format::xml);

        template <typename CharT>
        void
        save(std::basic_string_view<CharT> file_name,
             save_contents                 contents = save_contents::change_log,
             save_format                   format   = save_format::xml)
        {
            auto p = std::filesystem::path(file_name);
            save(p, contents, format);
        }

    private:
        std::shared_ptr<iplatform> m_platform;
    };
    //
} // namespace m::pil

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/sstring/sstring.h>
#include <m/strings/conversion_details.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>
#include <m/utility/view_converter.h>
#include <m/utility/zstring.h>

namespace m
{
    template <typename CharT>
    struct view_converter<CharT const*, std::basic_string_view<CharT>, void>
    {
        static constexpr std::basic_string_view<CharT>
        make_view(m::basic_zstring<CharT const> str) noexcept
        {
            if (str == nullptr)
                return std::basic_string_view<CharT>();

            return std::basic_string_view<CharT>(str);
        }
    };

    template <typename CharT>
    struct view_converter<std::basic_string_view<CharT>, std::basic_string_view<CharT>, void>
    {
        static inline constexpr std::basic_string_view<CharT>
        make_view(std::basic_string_view<CharT> view) noexcept
        {
            return view;
        }

        static inline constexpr std::optional<std::basic_string_view<CharT>>
        make_view(std::optional<std::basic_string_view<CharT>> const& view) noexcept
        {
            return view;
        }
    };

    template <typename CharT>
    struct view_converter<std::basic_string<CharT>, std::basic_string_view<CharT>, void>
    {
        static constexpr std::basic_string_view<CharT>
        make_view(std::basic_string<CharT> const& str) noexcept
        {
            return static_cast<std::basic_string_view<CharT>>(str);
        }

        static std::optional<std::basic_string_view<CharT>>
        make_view(std::optional<std::basic_string<CharT>> const& str) noexcept
        {
            if (!str.has_value())
                return std::nullopt;

            return make_view(str.value());
        }
    };

    template <typename CharT>
    struct view_converter<basic_sstring<CharT>, std::basic_string_view<CharT>, void>
    {
        static std::basic_string_view<CharT>
        make_view(basic_sstring<CharT> str)
        {
            return str.view();
        }

        static std::optional<std::basic_string_view<CharT>>
        make_view(std::optional<m::basic_sstring<CharT>> const& str)
        {
            if (!str.has_value())
                return std::nullopt;

            return make_view(str.value());
        }
    };

} // namespace m

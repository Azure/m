// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/utility/string_converter.h>

#include <m/sstring/sstring.h>
#include <m/strings/string_conversion_details.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>
#include <m/utility/zstring.h>

namespace m
{
    template <typename CharT>
    struct string_converter<CharT const*, std::basic_string_view<CharT>, void>
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
    struct string_converter<CharT const*, std::basic_string<CharT>, void>
    {
        static inline std::basic_string<CharT>
        make_string(m::basic_zstring<CharT const> str)
        {
            if (str == nullptr)
                return std::basic_string<CharT>();

            return std::basic_string<CharT>(str);
        }
    };

    template <typename CharT>
    struct string_converter<std::basic_string_view<CharT>, std::basic_string<CharT>, void>
    {
        static inline std::basic_string<CharT>
        make_string(std::basic_string_view<CharT> view)
        {
            return std::basic_string<CharT>(view);
        }

        static inline std::optional<std::basic_string<CharT>>
        make_string(std::optional<std::basic_string_view<CharT>> const& view)
        {
            if (!view.has_value())
                return std::nullopt;

            return make_string(view.value());
        }
    };

    template <typename CharT>
    struct string_converter<std::basic_string<CharT>, std::basic_string<CharT>, void>
    {
        static inline std::basic_string<CharT>
        make_string(std::basic_string<CharT> const& str)
        {
            return str;
        }

        static inline std::optional<std::basic_string<CharT>>
        make_string(std::optional<std::basic_string<CharT>> const& str)
        {
            return str;
        }
    };

    template <typename CharT>
    struct string_converter<std::basic_string_view<CharT>, std::basic_string_view<CharT>, void>
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
    struct string_converter<basic_sstring<CharT>, std::basic_string_view<CharT>, void>
    {
        static std::basic_string_view<CharT>
        make_view(basic_sstring<CharT> str)
        {
            return str.view();
        }
    };

    template <typename CharT>
    struct string_converter<basic_sstring<CharT>, std::basic_string<CharT>, void>
    {
        static std::basic_string<CharT>
        make_string(basic_sstring<CharT> str)
        {
            return std::basic_string(str.view());
        }
    };

    template <>
    struct string_converter<std::u16string_view, std::u8string, void>
    {
        static std::u8string
        make_string(std::u16string_view view);

        static std::optional<std::u8string>
        make_string(std::optional<std::u16string_view> const& view);
    };

    template <>
    struct string_converter<char16_t const*, std::u8string, void>
    {
        static std::u8string
        make_string(not_null<cu16zstring> str);

        static std::optional<std::u8string>
        make_string(cu16zstring str);
    };

    template <>
    struct string_converter<std::u16string, std::u8string, void>
    {
        static std::u8string
        make_string(std::u16string const& str);

        static std::optional<std::u8string>
        make_string(std::optional<std::u16string> const& str);
    };

    template <>
    struct string_converter<std::u32string_view, std::u8string, void>
    {
        static std::u8string
        make_string(std::u32string_view view);

        static std::optional<std::u8string>
        make_string(std::optional<std::u32string_view> const& view);
    };

    template <>
    struct string_converter<char32_t const*, std::u8string, void>
    {
        static std::u8string
        make_string(not_null<cu32zstring> str);

        static std::optional<std::u8string>
        make_string(cu32zstring str);
    };

    template <>
    struct string_converter<std::u32string, std::u8string, void>
    {
        static std::u8string
        make_string(std::u32string const& str);

        static std::optional<std::u8string>
        make_string(std::optional<std::u32string> const& str);
    };

    template <>
    struct string_converter<std::u8string_view, std::u16string, void>
    {
        static std::u16string
        make_string(std::u8string_view view);

        static std::optional<std::u16string>
        make_string(std::optional<std::u8string_view> const& view);
    };

    template <>
    struct string_converter<char8_t const*, std::u16string, void>
    {
        static std::u16string
        make_string(not_null<cu8zstring> str);

        static std::optional<std::u16string>
        make_string(cu8zstring str);
    };

    template <>
    struct string_converter<std::u8string, std::u16string, void>
    {
        static std::u16string
        make_string(std::u8string const& str);

        static std::optional<std::u16string>
        make_string(std::optional<std::u8string> const& str);
    };

    template <>
    struct string_converter<std::u32string_view, std::u16string, void>
    {
        static std::u16string
        make_string(std::u32string_view view);

        static std::optional<std::u16string>
        make_string(std::optional<std::u32string_view> const& view);
    };

    template <>
    struct string_converter<char32_t const*, std::u16string, void>
    {
        static std::u16string
        make_string(not_null<cu32zstring> str);

        static std::optional<std::u16string>
        make_string(cu32zstring str);
    };

    template <>
    struct string_converter<std::u32string, std::u16string, void>
    {
        static std::u16string
        make_string(std::u32string const& str);

        static std::optional<std::u16string>
        make_string(std::optional<std::u32string> const& str);
    };

    /// <summary>
    ///
    /// </summary>

    template <>
    struct string_converter<std::u8string_view, std::u32string, void>
    {
        static std::u32string
        make_string(std::u8string_view view);

        static std::optional<std::u32string>
        make_string(std::optional<std::u8string_view> const& view);
    };

    template <>
    struct string_converter<char8_t const*, std::u32string, void>
    {
        static std::u32string
        make_string(not_null<cu8zstring> str);

        static std::optional<std::u32string>
        make_string(cu8zstring str);
    };

    template <>
    struct string_converter<std::u8string, std::u32string, void>
    {
        static std::u32string
        make_string(std::u8string const& str);

        static std::optional<std::u32string>
        make_string(std::optional<std::u8string> const& str);
    };

    template <>
    struct string_converter<std::u16string_view, std::u32string, void>
    {
        static std::u32string
        make_string(std::u16string_view view);

        static std::optional<std::u32string>
        make_string(std::optional<std::u16string_view> const& view);
    };

    template <>
    struct string_converter<char16_t const*, std::u32string, void>
    {
        static std::u32string
        make_string(not_null<cu16zstring> str);

        static std::optional<std::u32string>
        make_string(cu16zstring str);
    };

    template <>
    struct string_converter<std::u16string, std::u32string, void>
    {
        static std::u32string
        make_string(std::u16string const& str);

        static std::optional<std::u32string>
        make_string(std::optional<std::u16string> const& str);
    };
} // namespace m::string_conversion_details

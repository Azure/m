// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>
#include <m/utility/zstring.h>

namespace m
{
    namespace string_conversion_details
    {
        // Partial specialization of functions is non-functional, so we use
        // a struct with static functions as an intermediate.
        //
        // It makes the task somewhat cumbersome but effective.
        //

        template <typename FromT, typename ToT>
            requires(m::character<typename ToT::value_type>)
        struct sch; // string conversion helper -- super wordy otherwise

        // We assume the null terminated string operations are the "lowest level",
        // so we form the views and then delegate to the basic_string_view
        // forms of the operations for any more intelligence.
        //

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        std::basic_string<ToCharT>
        czstring_to_string(m::not_null<m::basic_zstring<FromCharT const>> ptr)
        {
            return sch<std::basic_string_view<FromCharT>, std::basic_string<ToCharT>>::xlate(
                std::basic_string_view<FromCharT>(ptr));
        }

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        std::optional<std::basic_string<ToCharT>>
        czstring_to_opt_string(m::basic_zstring<FromCharT const> ptr)
        {
            if (ptr == nullptr)
                return std::nullopt;

            return czstring_to_string<FromCharT, ToCharT>(m::not_null(ptr));
        }

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        struct sch<FromCharT const*, std::optional<std::basic_string<ToCharT>>>
        {
            static std::optional<std::basic_string<ToCharT>>
            xlate(m::basic_zstring<FromCharT const> str)
            {
                return czstring_to_opt_string<FromCharT, ToCharT>(str);
            }
        };

        //
        // This specialization is a bit odd. It satisfies the desire to convert
        // a null terminated string to std::basic_string<>, but if it doesn't have
        // the m::not_null<> protection on it, it returns the std::optional<>
        // wrapper.
        //
        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        struct sch<FromCharT const*, std::basic_string<ToCharT>>
        {
            static std::optional<std::basic_string<ToCharT>>
            xlate(m::basic_zstring<FromCharT const> str)
            {
                if (str == nullptr)
                    return std::nullopt;

                return czstring_to_opt_string<FromCharT, ToCharT>(str);
            }
        };

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        struct sch<m::not_null<FromCharT const*>, std::basic_string<ToCharT>>
        {
            static std::basic_string<ToCharT>
            xlate(m::not_null<m::basic_zstring<FromCharT const>> str)
            {
                return czstring_to_string<FromCharT, ToCharT>(str);
            }
        };

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        std::basic_string<ToCharT>
        string_view_to_string(std::basic_string_view<FromCharT> const&);

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        struct sch<std::basic_string_view<FromCharT>, std::basic_string<ToCharT>>
        {
            static std::basic_string<ToCharT>
            xlate(std::basic_string_view<FromCharT> const& view)
            {
                return string_view_to_string<FromCharT, ToCharT>(view);
            }

            static void
            xlate(std::basic_string_view<FromCharT> const& from, std::basic_string<ToCharT>& to)
            {
                to = string_view_to_string<FromCharT, ToCharT>(from);
            }
        };

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        struct sch<std::optional<std::basic_string_view<FromCharT>>,
                   std::optional<std::basic_string<ToCharT>>>
        {
            static void
            xlate(std::optional<std::basic_string_view<FromCharT>> const& from,
                  std::optional<std::basic_string<ToCharT>>&              to)
            {
                if (!from.has_value())
                    to = std::nullopt;

                to = string_view_to_string<FromCharT, ToCharT>(from.value());
            }

            static std::optional<std::basic_string<ToCharT>>
            xlate(std::optional<std::basic_string_view<FromCharT>> const& from)
            {
                if (!from.has_value())
                    return std::nullopt;

                return string_view_to_string<FromCharT, ToCharT>(from.value());
            }
        };

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        std::basic_string<ToCharT>
        string_to_string(std::basic_string<FromCharT> const& str);

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        struct sch<std::basic_string<FromCharT>, std::basic_string<ToCharT>>
        {
            static void
            xlate(std::basic_string<FromCharT> const& from, std::basic_string<ToCharT>& to)
            {
                to = string_to_string<FromCharT, ToCharT>(from);
            }

            static std::basic_string<ToCharT>
            xlate(std::basic_string<FromCharT> const& from)
            {
                return string_to_string<FromCharT, ToCharT>(from);
            }
        };

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        struct sch<std::optional<std::basic_string<FromCharT>>,
                   std::optional<std::basic_string<ToCharT>>>
        {
            static void
            xlate(std::optional<std::basic_string<FromCharT>> const& from,
                  std::optional<std::basic_string<ToCharT>>&         to)
            {
                if (!from.has_value())
                    to = std::nullopt;

                to = string_to_string<FromCharT, ToCharT>(from.value());
            }

            static std::optional<std::basic_string<ToCharT>>
            xlate(std::optional<std::basic_string<FromCharT>> const& from)
            {
                if (!from.has_value())
                    return std::nullopt;

                return string_to_string<FromCharT, ToCharT>(from.value());
            }
        };

        //
        // Template instantiations for conversions that do not require any kind of
        // localization support are presented here.
        //
        // char's definition varies between CP_ACP and UTF-8 (Windows and Linux
        // respectively) by convention so the conversions are left up to platform
        // specific headers to choose these.
        //
        // wchar_t, similarly, is a choice between UTF-16 on Windows and UTF-32 on
        // Linux. So again, except for wchar_t <-> wchar_t, these conversions are
        // left up to per-platform headers to define.
        //

        //
        // First, views
        //

        template <>
        std::basic_string<char>
        string_view_to_string<char, char>(std::basic_string_view<char> const&);

        template <>
        std::basic_string<wchar_t>
        string_view_to_string<wchar_t, wchar_t>(std::basic_string_view<wchar_t> const&);

        template <>
        std::basic_string<char8_t>
        string_view_to_string<char8_t, char8_t>(std::basic_string_view<char8_t> const&);

        template <>
        std::basic_string<char8_t>
        string_view_to_string<char16_t, char8_t>(std::basic_string_view<char16_t> const&);

        template <>
        std::basic_string<char8_t>
        string_view_to_string<char32_t, char8_t>(std::basic_string_view<char32_t> const&);

        template <>
        std::basic_string<char16_t>
        string_view_to_string<char8_t, char16_t>(std::basic_string_view<char8_t> const&);
        template <>
        std::basic_string<char16_t>
        string_view_to_string<char16_t, char16_t>(std::basic_string_view<char16_t> const&);
        template <>
        std::basic_string<char16_t>
        string_view_to_string<char32_t, char16_t>(std::basic_string_view<char32_t> const&);

        template <>
        std::basic_string<char32_t>
        string_view_to_string<char8_t, char32_t>(std::basic_string_view<char8_t> const&);
        template <>
        std::basic_string<char32_t>
        string_view_to_string<char16_t, char32_t>(std::basic_string_view<char16_t> const&);
        template <>
        std::basic_string<char32_t>
        string_view_to_string<char32_t, char32_t>(std::basic_string_view<char32_t> const&);

        //
        // And then again for const refs to std::basic_string<CharT>
        //

        template <>
        std::basic_string<char>
        string_to_string<char, char>(std::basic_string<char> const&);

        template <>
        std::basic_string<wchar_t>
        string_to_string<wchar_t, wchar_t>(std::basic_string<wchar_t> const&);

        template <>
        std::basic_string<char8_t>
        string_to_string(std::basic_string<char8_t> const&);
        template <>
        std::basic_string<char8_t>
        string_to_string(std::basic_string<char16_t> const&);
        template <>
        std::basic_string<char8_t>
        string_to_string(std::basic_string<char32_t> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string(std::basic_string<char8_t> const&);
        template <>
        std::basic_string<char16_t>
        string_to_string(std::basic_string<char16_t> const&);

        template <>
        std::basic_string<char16_t>
        string_to_string(std::basic_string<char32_t> const&);

        template <>
        std::basic_string<char32_t>
        string_to_string(std::basic_string<char8_t> const&);
        template <>
        std::basic_string<char32_t>
        string_to_string(std::basic_string<char16_t> const&);
        template <>
        std::basic_string<char32_t>
        string_to_string(std::basic_string<char32_t> const&);

        // There must be a better way to do this but this brute force means will get the job done.
        template <typename T>
        using dont_use_t1 =
            std::conditional_t<std::is_same_v<T, char const*> || std::is_same_v<T, const char*>,
                               m::basic_zstring<char const>,
                               T>;

        template <typename T>
        using dont_use_t2 = std::conditional_t<std::is_same_v<T, wchar_t const*> ||
                                                   std::is_same_v<T, const wchar_t*>,
                                               m::basic_zstring<wchar_t const>,
                                               dont_use_t1<T>>;

        template <typename T>
        using dont_use_t3 = std::conditional_t<std::is_same_v<T, char8_t const*> ||
                                                   std::is_same_v<T, const char8_t*>,
                                               m::basic_zstring<char8_t const>,
                                               dont_use_t2<T>>;

        template <typename T>
        using dont_use_t4 = std::conditional_t<std::is_same_v<T, char16_t const*> ||
                                                   std::is_same_v<T, const char16_t*>,
                                               m::basic_zstring<char16_t const>,
                                               dont_use_t3<T>>;

        template <typename T>
        using dont_use_t5 = std::conditional_t<std::is_same_v<T, char32_t const*> ||
                                                   std::is_same_v<T, const char32_t*>,
                                               m::basic_zstring<char32_t const>,
                                               dont_use_t4<T>>;

        template <typename T>
        using from_type_t =
            dont_use_t5<std::remove_const_t<std::remove_reference_t<std::remove_const_t<T>>>>;

    } // namespace string_conversion_details

} // namespace m

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

        template <typename FromT, typename ToType>
            requires(character<value_type_of_t<ToType>>)
        struct sch; // string conversion helper -- super wordy otherwise

        // We assume the null terminated string operations are the "lowest level",
        // so we form the views and then delegate to the basic_string_view
        // forms of the operations for any more intelligence.
        //

        template <typename ToCharT, typename FromCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        std::basic_string<ToCharT>
        czstring_to_basic_string(m::basic_zstring<FromCharT const> ptr)
        {
            if (ptr == nullptr)
                return std::basic_string<ToCharT>();

            return sch<std::basic_string_view<FromCharT>, std::basic_string<ToCharT>>::make_string(
                std::basic_string_view<FromCharT>(ptr));
        }

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        std::basic_string<ToCharT>
        string_view_to_string(std::basic_string_view<FromCharT> const&);

        template <typename FromCharT, typename ToCharT>
            requires(m::character<FromCharT> && m::character<ToCharT>)
        std::basic_string<ToCharT>
        string_to_string(std::basic_string<FromCharT> const& str);

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

        template <typename T>
        using decay_remove_cvref_t = remove_cvref_t<std::decay_t<T>>;

        template <typename T>
        struct is_not_null
        {
            static inline constexpr bool value = false;
        };

        template <typename T>
        struct is_not_null<not_null<T>>
        {
            static inline constexpr bool value = true;
        };

        template <typename T>
        constexpr bool is_not_null_v = is_not_null<T>::value;

        template <typename T>
        struct plain_type
        {
            using type = decay_remove_cvref_t<T>;
        };

        template <typename T>
        struct not_null_type
        {
            using type = typename T::value_type;
        };

        template <typename T>
        struct conversion_strip
        {
            using type = typename plain_type<T>::type;
        };

        template <typename T>
        struct conversion_strip<not_null<T>>
        {
            using type = T;
        };

        template <typename T>
        struct conversion_strip<std::optional<T> const&>
        {
            using type = conversion_strip<T>::type;
        };

        template <typename T>
        struct conversion_strip<std::optional<T>&>
        {
            using type = conversion_strip<T>::type;
        };

        template <typename T>
        struct conversion_strip<T*>
        {
            using type = T*;
        };

        template <typename T>
        using conversion_strip_t = conversion_strip<T>::type;

        using T1 = const char(&)[100];
        using T2 = const char;
        using T3 = conversion_strip_t<T1>;

        inline auto& t3_id = typeid(T3);

        // static_assert(std::is_same_v<T2, T3>);
    } // namespace string_conversion_details

} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/make_span.h>

namespace m
{
    template <typename StringT>
    struct case_insensitive_less;

#if M_ENABLE_CASE_INSENSITIVE_LESS_ON_STD_STRING
    template <>
    struct case_insensitive_less<std::string>
    {
        struct case_insensitive_char_compare
        {
            constexpr bool
            operator()(char l, char r) const
            {
                return std::tolower(static_cast<unsigned char>(l)) <
                       std::tolower(static_cast<unsigned char>(r));
            }
        };

        constexpr bool
        operator()(std::string const& l, std::string const& r) const
        {
            return std::lexicographical_compare(
                l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
        }

        constexpr bool
        operator()(std::string_view const& l, std::string_view const& r) const
        {
            return std::lexicographical_compare(
                l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
        }
    };
#endif

    template <>
    struct case_insensitive_less<std::wstring>
    {
        struct case_insensitive_char_compare
        {
            constexpr bool
            operator()(wchar_t l, wchar_t r) const
            {
                static_assert(sizeof(wchar_t) <= sizeof(std::wint_t));

                return std::towlower(static_cast<std::wint_t>(l)) <
                       std::towlower(static_cast<std::wint_t>(r));
            }
        };

        constexpr bool
        operator()(std::wstring const& l, std::wstring const& r) const
        {
            return std::lexicographical_compare(
                l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
        }

        constexpr bool
        operator()(std::wstring_view const& l, std::wstring_view const& r) const
        {
            return std::lexicographical_compare(
                l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
        }
    };

    template <>
    struct case_insensitive_less<std::optional<std::wstring>>
    {
        struct case_insensitive_char_compare
        {
            constexpr bool
            operator()(wchar_t l, wchar_t r) const
            {
                static_assert(sizeof(wchar_t) <= sizeof(std::wint_t));

                return std::towlower(static_cast<std::wint_t>(l)) <
                       std::towlower(static_cast<std::wint_t>(r));
            }
        };

        constexpr bool
        operator()(std::optional<std::wstring> const& l, std::optional<std::wstring> const& r) const
        {
            if (!l.has_value())
                return r.has_value();

            if (!r.has_value())
                return false;

            return std::lexicographical_compare(l.value().begin(),
                                                l.value().end(),
                                                r.value().begin(),
                                                r.value().end(),
                                                case_insensitive_char_compare());
        }

        constexpr bool
        operator()(std::optional<std::wstring_view> const& l,
                   std::optional<std::wstring_view> const& r) const
        {
            if (!l.has_value())
                return r.has_value();

            if (!r.has_value())
                return false;

            return std::lexicographical_compare(l.value().begin(),
                                                l.value().end(),
                                                r.value().begin(),
                                                r.value().end(),
                                                case_insensitive_char_compare());
        }
    };

};

} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <string>
#include <string_view>

#include "platform.h"

namespace m::strings::impl::ordinal_case_insensitive
{
    struct case_insensitive_char_compare
    {
        constexpr bool
        operator()(char l, char r) const
        {
            return std::tolower(static_cast<unsigned char>(l)) <
                   std::tolower(static_cast<unsigned char>(r));
        }

        constexpr bool
        operator()(char8_t l, char8_t r) const
        {
            if (l < 128 && r < 128)
            {
                return std::tolower(static_cast<unsigned char>(l)) <
                       std::tolower(static_cast<unsigned char>(r));
            }

            return l < r;
        }

        constexpr bool
        operator()(char16_t l, char16_t r) const
        {
            if (l < 128 && r < 128)
            {
                return std::tolower(static_cast<unsigned char>(l)) <
                       std::tolower(static_cast<unsigned char>(r));
            }

            return l < r;
        }

        constexpr bool
        operator()(wchar_t l, wchar_t r) const
        {
            static_assert(sizeof(wchar_t) <= sizeof(std::wint_t));

            return std::towlower(static_cast<std::wint_t>(l)) <
                   std::towlower(static_cast<std::wint_t>(r));
        }

        constexpr bool
        operator()(char32_t l, char32_t r) const
        {
            static_assert(sizeof(char32_t) <= sizeof(std::wint_t));

            return std::towlower(static_cast<std::wint_t>(l)) <
                   std::towlower(static_cast<std::wint_t>(r));
        }
    };

    bool
    less(std::string_view const& l, std::string_view const& r)
    {
        return std::lexicographical_compare(
            l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
    }

    bool
    less(std::wstring_view const& l, std::wstring_view const& r)
    {
        return std::lexicographical_compare(
            l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
    }

    bool
    less(std::u8string_view const& l, std::u8string_view const& r)
    {
        return std::lexicographical_compare(
            l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
    }

    bool
    less(std::u16string_view const& l, std::u16string_view const& r)
    {
        return std::lexicographical_compare(
            l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
    }

    bool
    less(std::u32string_view const& l, std::u32string_view const& r)
    {
        return std::lexicographical_compare(
            l.begin(), l.end(), r.begin(), r.end(), case_insensitive_char_compare());
    }

    //
} // namespace m::strings::impl::ordinal_case_insensitive

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/strings/platform_independent_string_conversions.h>
#include <m/strings/string_conversion_details.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/pointers.h>
#include <m/utility/zstring.h>

namespace m
{
    //
    // to_string
    //

    template <typename FromT>
    auto
    to_string(FromT&& from)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;

        return string_conversion_details::sch<conversion_from_t, std::string>::make_string(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_string(FromT&& from, std::string& str)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;

        auto ret = string_conversion_details::sch<conversion_from_t, std::string>::make_string(
            std::forward<FromT>(from));
        using std::swap;
        swap(ret, str);
    }

    //
    //  m::to_wstring
    //

    template <typename FromT>
    decltype(auto)
    to_wstring(FromT&& from)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;

        return string_conversion_details::sch<conversion_from_t, std::wstring>::make_string(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_wstring(FromT&& from, std::wstring& str)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;

        auto ret = string_conversion_details::sch<conversion_from_t, std::wstring>::make_string(
            std::forward<FromT>(from));
        using std::swap;
        swap(ret, str);
    }

    template <typename FromT>
    auto
    to_u8string(FromT&& from)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;

        return string_conversion_details::sch<conversion_from_t, std::u8string>::make_string(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_u8string(FromT&& from, std::u8string& str)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;
        auto ret = string_conversion_details::sch<conversion_from_t, std::u8string>::make_string(
            std::forward<FromT>(from));
        using std::swap;
        swap(ret, str);
    }

    template <typename FromT>
    auto
    to_u16string(FromT&& from)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;

        return string_conversion_details::sch<conversion_from_t, std::u16string>::make_string(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_u16string(FromT&& from, std::u16string& str)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;
        auto ret = string_conversion_details::sch<conversion_from_t, std::u16string>::make_string(
            std::forward<FromT>(from));
        using std::swap;
        swap(ret, str);
    }

    template <typename FromT>
    auto
    to_u32string(FromT&& from)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;

        return string_conversion_details::sch<conversion_from_t, std::u32string>::make_string(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_u32string(FromT&& from, std::u32string& str)
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;
        auto ret = string_conversion_details::sch<conversion_from_t, std::u32string>::make_string(
            std::forward<FromT>(from));
        using std::swap;
        swap(ret, str);
    }
} // namespace m

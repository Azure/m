// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

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
    using stripped_t = std::remove_const_t<
        std::remove_volatile_t<std::remove_reference_t<std::remove_const_t<FromT>>>>;

    template <typename FromT>
    auto
    to_string(FromT&& from)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::sch<from_type_to_use, std::string>::xlate(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_string(FromT&& from, std::string& str)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        string_conversion_details::sch<from_type_to_use, std::remove_reference_t<decltype(str)>>::
            xlate(std::forward<FromT>(from), str);
    }

    //
    //  m::to_wstring
    //

    template <typename FromT>
    auto
    to_wstring(FromT&& from)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::sch<from_type_to_use, std::wstring>::xlate(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_wstring(FromT&& from, std::wstring& str)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::
            sch<from_type_to_use, std::remove_reference_t<decltype(str)>>::xlate(
                std::forward<FromT>(from), str);
    }

    template <typename FromT>
    auto
    to_u8string(FromT&& from)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::sch<from_type_to_use, std::u8string>::xlate(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_u8string(FromT&& from, std::u8string& str)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::
            sch<from_type_to_use, std::remove_reference_t<decltype(str)>>::xlate(
                std::forward<FromT>(from), str);
    }

    template <typename FromT>
    auto
    to_u16string(FromT&& from)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::sch<from_type_to_use, std::u16string>::xlate(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_u16string(FromT&& from, std::u16string& str)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::
            sch<from_type_to_use, std::remove_reference_t<decltype(str)>>::xlate(
                std::forward<FromT>(from), str);
    }

    template <typename FromT>
    auto
    to_u32string(FromT&& from)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::sch<from_type_to_use, std::u32string>::xlate(
            std::forward<FromT>(from));
    }

    template <typename FromT>
    void
    to_u32string(FromT&& from, std::u32string& str)
    {
        using from_type_to_use = string_conversion_details::from_type_t<FromT>;

        return string_conversion_details::
            sch<from_type_to_use, std::remove_reference_t<decltype(str)>>::xlate(
                std::forward<FromT>(from), str);
    }
} // namespace m

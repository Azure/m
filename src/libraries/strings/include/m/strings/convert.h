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

#if 0
    std::optional<std::string>
    to_string(czstring str);

    std::string
    to_string(m::not_null<czstring> str);

    std::string
    to_string(std::string_view v);

    void
    to_string(std::string_view v, std::string& str);

    std::string
    to_string(std::string const& s);

    void
    to_string(std::string const& s, std::string& str);

    std::optional<std::string>
    to_string(std::optional<std::string_view> v);

    void
    to_string(std::optional<std::string_view> v, std::optional<std::string>& str);
#endif

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

#if 0
    void to_wstring(std::nullptr_t) = delete;

    std::optional<std::wstring>
    to_wstring(cwzstring str);

    std::wstring
    to_wstring(m::not_null<cwzstring> str);

    std::wstring
    to_wstring(std::wstring_view v);

    void
    to_wstring(std::wstring_view v, std::wstring& str);

    std::wstring
    to_wstring(std::wstring const& s);

    void
    to_wstring(std::wstring const& s, std::wstring& str);

    std::optional<std::wstring>
    to_wstring(std::optional<std::wstring_view> v);

    void
    to_wstring(std::optional<std::wstring_view> v, std::optional<std::wstring>& str);

    std::optional<std::wstring>
    to_wstring(std::optional<std::wstring> const& s);

    void
    to_wstring(std::optional<std::wstring> s, std::optional<std::wstring>& str);

    //
    // to_u8string
    //

    void to_u8string(std::nullptr_t) = delete;

    std::optional<std::u8string>
    to_u8string(cu8zstring str);

    std::u8string
    to_u8string(m::not_null<cu8zstring> str);

    std::optional<std::u8string>
    to_u8string(cu16zstring ptr);

    std::u8string
    to_u8string(m::not_null<cu16zstring> ptr);

    std::optional<std::u8string>
    to_u8string(cu32zstring ptr);

    std::u8string
    to_u8string(m::not_null<cu32zstring> ptr);

    std::u8string
    to_u8string(std::u8string_view v);

    void
    to_u8string(std::u8string_view v, std::u8string& str);

    std::u8string
    to_u8string(std::u8string const& s);

    void
    to_u8string(std::u8string const& s, std::u8string& str);

    std::optional<std::u8string>
    to_u8string(std::optional<std::u8string_view> v);

    void
    to_u8string(std::optional<std::u8string_view> v, std::optional<std::u8string>& str);

    //
    //  Transcoding
    //

    //
    // std::u16string -> std::u8string
    // std::u16string_view -> std::u8string
    // std::optional<std::u16string_view> -> std::optional<std::u8string>
    //

    void
    to_u8string(std::u16string_view v, std::u8string& str);

    std::u8string
    to_u8string(std::u16string_view v);

    void
    to_u8string(std::u16string const& s, std::u8string& str);

    std::u8string
    to_u8string(std::u16string const& s);

    void
    to_u8string(std::optional<std::u16string_view> v, std::optional<std::u8string>& str);

    std::optional<std::u8string>
    to_u8string(std::optional<std::u16string_view> v);

    void
    to_u8string(std::u32string_view v, std::u8string& str);

    std::u8string
    to_u8string(std::u32string_view v);

    void
    to_u8string(std::u32string const& s, std::u8string& str);

    std::u8string
    to_u8string(std::u32string const& s);

    void
    to_u8string(std::optional<std::u32string_view> v, std::optional<std::u8string>& str);

    std::optional<std::u8string>
    to_u8string(std::optional<std::u32string_view> v);

    //
    // to_u16string
    //

    void to_u16string(std::nullptr_t) = delete;

    std::optional<std::u16string>
    to_u16string(cu8zstring ptr);

    std::u16string
    to_u16string(m::not_null<cu8zstring> ptr);

    std::optional<std::u16string>
    to_u16string(cu16zstring ptr);

    std::u16string
    to_u16string(m::not_null<cu16zstring> ptr);

    std::optional<std::u16string>
    to_u16string(cu32zstring ptr);

    std::u16string
    to_u16string(m::not_null<cu32zstring> ptr);

    //
    // std::u8string -> std::u16string
    // std::u8string_view -> std::u16string
    // std::optional<std::u8string_view> -> std::optional<std::u16string>
    //

    void
    to_u16string(std::u8string_view v, std::u16string& str);

    std::u16string
    to_u16string(std::u8string_view v);

    void
    to_u16string(std::u8string const& s, std::u16string& str);

    std::u16string
    to_u16string(std::u8string const& s);

    void
    to_u16string(std::optional<std::u8string_view> v, std::optional<std::u16string>& str);

    std::optional<std::u16string>
    to_u16string(std::optional<std::u8string_view> v);

    std::u16string
    to_u16string(std::u16string_view v);

    void
    to_u16string(std::u16string_view v, std::u16string& str);

    std::u16string
    to_u16string(std::u16string const& s);

    void
    to_u16string(std::u16string const& s, std::u16string& str);

    std::optional<std::u16string>
    to_u16string(std::optional<std::u16string_view> v);

    void
    to_u16string(std::optional<std::u16string_view> v, std::optional<std::u16string>& str);

    //
    // std::u32string -> std::u16string
    // std::u32string_view -> std::u16string
    // std::optional<std::u32string_view> -> std::optional<std::u16string>
    //

    void
    to_u16string(std::u32string_view v, std::u16string& str);

    std::u16string
    to_u16string(std::u32string_view v);

    void
    to_u16string(std::u32string const& s, std::u16string& str);

    std::u16string
    to_u16string(std::u32string const& s);

    void
    to_u16string(std::optional<std::u32string_view> v, std::optional<std::u16string>& str);

    std::optional<std::u16string>
    to_u16string(std::optional<std::u32string_view> v);

    //
    // to_u32string
    //

    void to_u32string(std::nullptr_t) = delete;

    std::u32string
    to_u32string(char8_t const* ptr);

    std::u32string
    to_u32string(char16_t const* ptr);

    std::u32string
    to_u32string(char32_t const* ptr);

    void
    to_u32string(std::u8string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::u8string_view v);

    void
    to_u32string(std::u8string const& s, std::u32string& str);

    std::u32string
    to_u32string(std::u8string const& s);

    void
    to_u32string(std::optional<std::u8string_view> v, std::optional<std::u32string>& str);

    std::optional<std::u32string>
    to_u32string(std::optional<std::u8string_view> v);

    void
    to_u32string(std::u16string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::u16string_view v);

    std::u32string
    to_u32string(std::u16string const& s);

    void
    to_u32string(std::u16string const& s, std::u32string& str);

    std::optional<std::u32string>
    to_u32string(std::optional<std::u16string_view> v);

    void
    to_u32string(std::optional<std::u16string_view> v, std::optional<std::u32string>& str);

    std::u32string
    to_u32string(std::u32string_view v);

    void
    to_u32string(std::u32string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::u32string const& s);

    void
    to_u32string(std::u32string const& s, std::u32string& str);

    std::optional<std::u32string>
    to_u32string(std::optional<std::u32string_view> v);

    void
    to_u32string(std::optional<std::u32string_view> v, std::optional<std::u32string>& str);
#endif
} // namespace m

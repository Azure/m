// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

#include <m/utility/string_converter.h>

#include <m/sstring/sstring.h>
#include <m/strings/platform_independent_string_conversions.h>
#include <m/strings/string_conversion_details.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/algorithm.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>
#include <m/utility/type_traits.h>
#include <m/utility/zstring.h>

namespace m
{
    //
    // to_string
    //

    template <typename ToCharT, typename FromT>
    auto
    to_basic_string(FromT&& from)
    {
        auto const view = view_of(std::forward<FromT>(from));
        using view_t    = remove_optional_t<remove_cvref_t<decltype(view)>>;
        // using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;
        using sch_t = string_converter<view_t, std::basic_string<ToCharT>>;
        return sch_t::make_string(view);
    }

    template <typename FromT, typename ToCharT>
        requires(m::character<ToCharT> && m::stringish<FromT>)
    void
    to_basic_string(FromT&& from, std::basic_string<ToCharT>& str)
    {
        auto const view = view_of(std::forward<FromT>(from));
        using view_t    = remove_optional_t<remove_cvref_t<decltype(view)>>;
        // using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;
        using sch_t = string_converter<view_t, std::basic_string<ToCharT>, void>;
        auto ret    = sch_t::make_string(view);
        using std::swap;
        swap(ret, str);
    }

    template <typename FromT>
    //        requires m::stringish<FromT>
    auto
    to_string(FromT&& from)
    {
        return to_basic_string<char>(view_of(std::forward<FromT>(from)));
    }

    template <typename FromT>
    void
    to_string(FromT&& from, std::string& to)
    {
        to_basic_string(view_of(std::forward<FromT>(from)), to);
    }

    //
    //  m::to_wstring
    //

    template <typename FromT>
    decltype(auto)
    to_wstring(FromT&& from)
    {
        return to_basic_string<wchar_t>(view_of(std::forward<FromT>(from)));
    }

    template <typename FromT>
    void
    to_wstring(FromT&& from, std::wstring& to)
    {
        to_basic_string(view_of(std::forward<FromT>(from)), to);
    }

    template <typename FromT>
    auto
    to_u8string(FromT&& from)
    {
        return to_basic_string<char8_t>(view_of(std::forward<FromT>(from)));
    }

    template <typename FromT>
    void
    to_u8string(FromT&& from, std::u8string& to)
    {
        to_basic_string(view_of(std::forward<FromT>(from)), to);
    }

    template <typename FromT>
    auto
    to_u16string(FromT&& from)
    {
        return to_basic_string<char16_t>(view_of(std::forward<FromT>(from)));
    }

    template <typename FromT>
    void
    to_u16string(FromT&& from, std::u16string& to)
    {
        to_basic_string(view_of(std::forward<FromT>(from)), to);
    }

    template <typename FromT>
    auto
    to_u32string(FromT&& from)
    {
        return to_basic_string<char32_t>(view_of(std::forward<FromT>(from)));
    }

    template <typename FromT>
    void
    to_u32string(FromT&& from, std::u32string& to)
    {
        to_basic_string(view_of(std::forward<FromT>(from)), to);
    }

    //
    // And again for m::basic_sstring<>
    //
    // The fan-out for all the various conversions is performed across the
    // string_converter<From, To> types because there are a myriad of algorithms and
    // optimizations that are performed on a per-platform basis.
    //
    // basic_sstring<> is somewhat simple. It needs a std::string_view<> or
    // something more or less equivalent, and it must copy it in. As such,
    // there is less opportunity for the participatory buffer management
    // algorithms that the other string conversion implementations use.
    //
    // Instead of expecing all the string_converter<>s to implement make_sstring()
    // (which is what I initially started doing - and created a mess by
    // doubling the number of static helper functions implemented),
    // instead the sstring support will always build the character string
    // into a std::basic_string<> (probably should be a m::string_buffer<>
    // but needs validation) and then copied into the basic_sstring.
    //
    // It would be ideal if there was some pattern of building a value
    // into a basic_sstring, but it's not there yet.
    //

    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    string_conversion_details::basic_sstring_with_equivalent_optionality_t<ToCharT, m::remove_cvref_t<FromT>>
    to_basic_sstring(FromT&& from)
    {
        auto const view = view_of(std::forward<FromT>(from));
        using view_t    = remove_optional_t<remove_cvref_t<decltype(view)>>;
        // using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;
        using sch_t         = string_converter<view_t, std::basic_string<ToCharT>, void>;
        auto temp_string    = sch_t::make_string(view);

        if constexpr (std::is_same_v<decltype(temp_string), std::optional<std::basic_string<ToCharT>>>)
        {
            if (!temp_string.has_value())
                return std::nullopt;

            return temp_string.value();
        }
        else
        {
            return temp_string;
        }
    }

    template <typename FromT>
    auto
    to_sstring(FromT&& from)
    {
        return to_basic_sstring<char>(std::forward<FromT>(from));
    }

    template <typename FromT>
    decltype(auto)
    to_wsstring(FromT&& from)
    {
        return to_basic_sstring<wchar_t>(std::forward<FromT>(from));
    }

    template <typename FromT>
    auto
    to_u8sstring(FromT&& from)
    {
        return to_basic_sstring<char8_t>(std::forward<FromT>(from));
    }

    template <typename FromT>
    auto
    to_u16sstring(FromT&& from)
    {
        return to_basic_sstring<char16_t>(std::forward<FromT>(from));
    }

    template <typename FromT>
    auto
    to_u32sstring(FromT&& from)
    {
        return to_basic_sstring<char32_t>(std::forward<FromT>(from));
    }

} // namespace m

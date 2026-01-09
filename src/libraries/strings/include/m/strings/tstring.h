// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/utility/string_converter.h>

#include <m/strings/convert.h>
#include <m/strings/conversion_details.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>
#include <m/utility/string_converter.h>
#include <m/utility/view_converter.h>
#include <m/utility/zstring.h>

namespace m
{
    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    auto
    to_basic_string_view_t(FromT&& from) noexcept
    {
        using conversion_from_t = m::conversion_details::template conversion_strip_t<FromT>;
        return view_converter<conversion_from_t, std::basic_string_view<ToCharT>, void>::make_view(
            std::forward<FromT>(from));
    }

    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    std::basic_string<ToCharT>
    to_basic_string_t(FromT&& from) noexcept
    {
        using conversion_from_t = m::conversion_details::template conversion_strip_t<FromT>;
        return string_converter<conversion_from_t, std::basic_string<ToCharT>, void>::make_string(
            std::forward<FromT>(from));
    }
} // namespace m

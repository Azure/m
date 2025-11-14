// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <m/strings/convert.h>
#include <m/strings/string_conversion_details.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>
#include <m/utf/transcode.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>
#include <m/utility/string_conversion.h>
#include <m/utility/zstring.h>

namespace m
{
    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    decltype(auto)
    to_string_view_t(FromT&& from) noexcept
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;
        return string_conversion_details::sch<conversion_from_t, std::basic_string_view<ToCharT>>::
            make_view(std::forward<FromT>(from));
    }

    template <typename ToCharT, typename FromT>
        requires(m::character<ToCharT>)
    std::basic_string<ToCharT>
    to_string_t(FromT&& from) noexcept
    {
        using conversion_from_t = string_conversion_details::conversion_strip_t<FromT>;
        return string_conversion_details::sch<conversion_from_t, std::basic_string<ToCharT>>::
            make_string(std::forward<FromT>(from));
    }
} // namespace m

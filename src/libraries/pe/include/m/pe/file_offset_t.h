// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <cstdint>
#include <format>

#include <m/byte_streams/byte_streams.h>
#include <m/math/math.h>

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        enum class file_offset_t : uint32_t;
    } // namespace pe
} // namespace m

constexpr m::pe::file_offset_t
operator+(m::pe::file_offset_t l, m::io::offset_t r)
{
    return m::pe::file_offset_t{m::math::add(std::to_underlying(l),
                                             std::to_underlying(r),
                                             std::underlying_type_t<m::pe::file_offset_t>{})};
}

constexpr m::io::position_t
operator+(m::io::position_t l, m::pe::file_offset_t r)
{
    return m::io::position_t(m::math::add(std::to_underlying(l),
                                          std::to_underlying(r),
                                          std::underlying_type_t<m::pe::file_offset_t>{}));
}

template <>
struct std::formatter<m::pe::file_offset_t, wchar_t>
{
    using file_offset_t = m::pe::file_offset_t;

    formatter()                 = default;
    formatter(formatter const&) = default;
    formatter(formatter&&)      = default;
    ~formatter()                = default;

    formatter&
    operator=(formatter const& other)
    {
        // no state??
        return *this;
    }

    template <typename ParseContext>
    constexpr decltype(auto)
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(file_offset_t fo, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), L"{{ OFF {:#x} }}", std::to_underlying(fo));
    }
};

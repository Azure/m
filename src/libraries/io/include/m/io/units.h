// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <cstdint>
#include <format>
#include <type_traits>
#include <utility>

#include <m/cast/to.h>

#include <m/math/math.h>
#include <m/utility/utility.h>

namespace m
{
    namespace io
    {
        enum class offset_t : int64_t
        {
        };

        enum class position_t : uint64_t
        {
        };
    } // namespace io
} // namespace m

M_INTEGER_RELATIONAL_OPERATORS(m::io::offset_t);
M_INTEGER_OPERATIONS_INC_DEC(m::io::offset_t);
M_INTEGER_OPERATIONS_PLUS_MINUS(m::io::offset_t);
M_INTEGER_OPERATIONS_PLUS_SIZE_T(m::io::offset_t);

M_INTEGER_RELATIONAL_OPERATORS(m::io::position_t);
M_INTEGER_OPERATIONS_INC_DEC(m::io::position_t);
M_INTEGER_OPERATIONS_PLUS_SIZE_T(m::io::position_t);
M_INTEGER_OPERATIONS_PLUSSES_NODECAY(m::io::position_t, m::io::offset_t, m::io::position_t);
M_INTEGER_OPERATIONS_MINUSES(m::io::position_t, m::io::position_t, m::io::offset_t);

M_INTEGER_OPERATIONS_MINUS_T_(m::io::position_t,
                              std::underlying_type_t<m::io::offset_t>,
                              std::underlying_type_t<m::io::position_t>);

M_INTEGER_OPERATIONS_MINUSEQUALS_(m::io::position_t, std::underlying_type_t<m::io::offset_t>);

constexpr auto
operator/(m::io::offset_t l, std::size_t r)
{
    return m::io::offset_t{
        m::try_cast<std::underlying_type_t<m::io::offset_t>>(std::to_underlying(l) / r)};
}

//
// operators for m::io::position_t
//


template <>
struct std::formatter<m::io::offset_t, wchar_t>
{
    using offset_t = m::io::offset_t;

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
    format(offset_t p, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), L"^{:#x}", std::to_underlying(p));
    }
};

template <>
struct std::formatter<m::io::position_t, wchar_t>
{
    using position_t = m::io::position_t;

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
    format(position_t p, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), L"@{:#x}", std::to_underlying(p));
    }
};

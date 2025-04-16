// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <compare>
#include <cstdint>
#include <exception>
#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <m/math/math.h>
#include <m/math/integer_functor_macros.h>
#include <m/utility/utility.h>

#include "file_offset_t.h"

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        enum class rva_t : uint32_t;
    } // namespace pe
} // namespace m

M_INTEGER_RELATIONAL_OPERATORS(m::pe::rva_t);
M_INTEGER_OPERATIONS_INC_DEC(m::pe::rva_t);
M_INTEGER_OPERATIONS_PLUS_T(m::pe::rva_t, std::size_t);
M_INTEGER_OPERATIONS_PLUS_T(m::pe::rva_t, uint32_t);
M_INTEGER_OPERATIONS_MINUS_SIZE_T(m::pe::rva_t);
M_INTEGER_OPERATIONS_PLUSSES(m::pe::rva_t, m::io::offset_t, m::pe::rva_t);
M_INTEGER_OPERATIONS_PLUSSES(m::io::offset_t, m::pe::rva_t, m::pe::rva_t);
M_INTEGER_OPERATIONS_MINUSES_NODECAY(m::pe::rva_t, m::pe::rva_t, m::pe::offset_t);

template <>
struct std::formatter<m::pe::rva_t, wchar_t>
{
    using rva_t = m::pe::rva_t;

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
    format(rva_t rva, FormatContext& ctx) const
    {
        return std::format_to(ctx.out(), L"{{ RVA {:#x} }}", std::to_underlying(rva));
    }
};

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <format>
#include <string_view>

using namespace std::string_view_literals;

namespace m
{
    namespace pe
    {
        enum class image_magic_t : uint16_t
        {
            pe32     = 0x10b,
            pe32plus = 0x20b,
        };
    }
} // namespace m


template <>
struct std::formatter<m::pe::image_magic_t, wchar_t>
{
    using image_magic_t = m::pe::image_magic_t;

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
    format(image_magic_t magic, FormatContext& ctx) const
    {
        auto const x = static_cast<uint16_t>(magic); // cast to underlying type

        wstring_view name = L"<no mapping>"sv;

        // TODO: Round out this mapping, maybe move to utility function?
        switch (magic)
        {
            default: break;
            case image_magic_t::pe32: name = L"pe32"sv; break;
            case image_magic_t::pe32plus: name = L"pe32plus"sv; break;
        }

        return std::format_to(ctx.out(), L"{} ({:#x})", name, x);
    }
};

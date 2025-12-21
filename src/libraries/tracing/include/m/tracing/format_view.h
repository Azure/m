// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <format>
#include <string_view>

#include <m/error_handling/macros.h>

namespace m
{
    namespace tracing
    {
        template <typename CharT>
        struct format_view
        {
            constexpr format_view(std::basic_string_view<CharT> view) noexcept: m_view(view) {}
            constexpr format_view(CharT const* ptr) noexcept: m_view()
            {
                if (ptr != nullptr)
                    m_view = std::basic_string_view<CharT>(ptr);
            }
            std::basic_string_view<CharT> m_view;
        };

        template <typename CharT>
        format_view(std::basic_string_view<CharT> v) -> format_view<CharT>;

    } // namespace tracing
} // namespace m

template <typename OutCharT, typename ViewCharT>
struct std::formatter<m::tracing::format_view<ViewCharT>, OutCharT>
{
    using fmtview = m::tracing::format_view<ViewCharT>;

    template <typename ParseContext>
    constexpr decltype(auto)
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it == 'q')
        {
            m_enquote = true;
            it++;
        }

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(fmtview const& v, FormatContext& ctx) const
    {
        auto       it  = v.m_view.begin();
        auto const end = v.m_view.end();
        auto       out = ctx.out();

        if (m_enquote)
            *out++ = '\"';

        while (it != end)
        {
            auto const ch = *it++;

            if (m_enquote && ch == '"')
            {
                *out++ = '\\';
                *out++ = '"';
            }
            else if (ch >= 32 && ch <= 127)
                *out++ = static_cast<OutCharT>(ch);
            else
            {
                if constexpr (::std::is_same_v<OutCharT, char>)
                {
                    out = std::format_to(out, "\\U+{:04x}", static_cast<uintmax_t>(ch));
                }
                else if constexpr (::std::is_same_v<OutCharT, wchar_t>)
                {
                    out = std::format_to(out, L"\\U+{:04x}", static_cast<uintmax_t>(ch));
                }
                else
                {
                    M_NOT_IMPLEMENTED("only char and wchar_t implemented");
                }
            }
        }

        if (m_enquote)
            *out++ = '\"';

        return out;
    }

    bool m_enquote{false};
};

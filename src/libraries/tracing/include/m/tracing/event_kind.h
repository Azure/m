// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <format>

namespace m
{
    namespace tracing
    {
        enum class event_kind : uint8_t
        {
            critical,    // to be called out specifically
            error,       // obvious
            information, // general useful operational information
            verbose,     // more useful information not on by default
            tracing,     // sometimes called "painful" - tracing in great detail
        };
    } // namespace tracing
} // namespace m

template <typename CharT>
struct std::formatter<m::tracing::event_kind, CharT>
{
    template <typename ParseContext>
    constexpr decltype(auto)
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it == 'b')
        {
            it++;
            m_brief = true;
        }

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(m::tracing::event_kind kind, FormatContext& ctx) const
    {
        auto out = ctx.out();

        string_view kind_sv = m_brief ? "?"sv : "<unmapped>"sv;

        switch (kind)
        {
            case m::tracing::event_kind::critical: kind_sv = m_brief ? "!!"sv : "critical"sv; break;
            case m::tracing::event_kind::error: kind_sv = m_brief ? "!"sv : "error"sv; break;
            case m::tracing::event_kind::information:
                kind_sv = m_brief ? "-"sv : "information"sv;
                break;
            case m::tracing::event_kind::tracing: kind_sv = m_brief ? "tr"sv : "tracing"sv; break;
            case m::tracing::event_kind::verbose: kind_sv = m_brief ? "v"sv : "verbose"sv; break;
            default: kind_sv = m_brief ? "?"sv : "<unmapped>"sv; break;
        }

        if (m_brief)
            out = std::format_to(out, "{}", kind_sv);
        else
            out = std::format_to(out, "{{ m::tracing::event_kind::{} }}", kind_sv);

        return out;
    }

    bool m_brief{false};
};
// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

namespace m::tracing
{
    enum class on_message_disposition
    {
        message_forwarded,
        message_processed,
    };
} // namespace m::tracing

template <typename CharT>
struct std::formatter<m::tracing::on_message_disposition, CharT>
{
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
    format(m::tracing::on_message_disposition mqo, FormatContext& ctx) const
    {
        auto out = ctx.out();

        string_view option_sv = "<unmapped>"sv;

        switch (mqo)
        {
            case m::tracing::on_message_disposition::message_forwarded:
                option_sv = "message_forwarded"sv;
                break;
            case m::tracing::on_message_disposition::message_processed:
                option_sv = "message_processed"sv;
                break;
            default: option_sv = "<unmapped>"sv; break;
        }

        out = std::format_to(out, "{{ m::tracing::on_message_disposition::{} }}", option_sv);

        return out;
    }
};

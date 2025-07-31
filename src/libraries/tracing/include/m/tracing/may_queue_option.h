// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <format>

namespace m
{
    namespace tracing
    {
        enum class may_queue_option
        {
            may_queue,
            may_not_queue,
        };
    } // namespace tracing
} // namespace m

template <typename CharT>
struct std::formatter<m::tracing::may_queue_option, CharT>
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
    format(m::tracing::may_queue_option mqo, FormatContext& ctx) const
    {
        auto out = ctx.out();

        string_view option_sv = "<unmapped>"sv;

        switch (mqo)
        {
            case m::tracing::may_queue_option::may_queue: option_sv = "may_queue"sv; break;
            case m::tracing::may_queue_option::may_not_queue: option_sv = "may_not_queue"sv; break;
            default: option_sv = "<unmapped>"sv; break;
        }

        out = std::format_to(out, "{{ m::tracing::may_queue_option::{} }}", option_sv);

        return out;
    }
};
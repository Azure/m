// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <format>

namespace m
{
    namespace tracing
    {
        /// <summary>
        /// The `close_flush_option` enumerate is used to direct the
        /// `m::tracing::sink.close()` virtual member function overrides'
        /// behavior.
        /// 
        /// The expectation is that the sinks will "normally"
        /// drain their queues in their close operations, waiting for the
        /// items to process, possibly on remote threads of execution before
        /// the `close()` function returns.
        /// 
        /// However if the process is terminating for some cause like a user
        /// mode internal error check, there may be reason to try to flush
        /// the queues more immediately. When this happens, a call with
        /// `close_flush_option::expedite` would be made.
        /// 
        /// If no blocking can be afforded, `close()` may be called with
        /// `close_flush_option::abandon` which gives the sink one chance to
        /// perhaps write a message with a summary of the number of unwritten
        /// messages still in the queue, but the sink should not attempt
        /// to drain the queue, it should set a flag to prevent any further
        /// processing by workers on other threads and it should otherwise
        /// stop processing - the process is about to terminate. This is a
        /// courtesy call.
        /// 
        /// More on this overall design debate will probably be present across
        /// the m::tracing::sink and M_INTERNAL_ERROR_CHECK() macro definition.
        /// 
        /// </summary>
        enum class close_flush_option
        {
            normal,
            expedite,
            abandon,
        };
    } // namespace tracing
} // namespace m

template <typename CharT>
struct std::formatter<m::tracing::close_flush_option, CharT>
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
    format(m::tracing::close_flush_option cfo, FormatContext& ctx) const
    {
        auto out = ctx.out();

        string_view option_sv = "<unmapped>"sv;

        switch (cfo)
        {
            case m::tracing::close_flush_option::normal: option_sv = "normal"sv; break;
            case m::tracing::close_flush_option::expedite:
                option_sv = "expedite"sv;
                break;
            case m::tracing::close_flush_option::abandon: option_sv = "abandon"sv; break;
            default: option_sv = "<unmapped>"sv; break;
        }

        out = std::format_to(out, "{{ m::tracing::close_flush_option::{} }}", option_sv);

        return out;
    }
};
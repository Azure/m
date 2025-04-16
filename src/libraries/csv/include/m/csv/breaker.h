// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <exception>
#include <format>
#include <functional>
#include <iterator>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <m/io/units.h>

namespace m
{
    namespace csv
    {
        enum class break_reason
        {
            field,
            row,
        };

        //
        //
        //


        //
        // The value cache_line_length should be std::hardware_constructive_interference_size
        // from the <new> header but it seems to be missing in the clang build so instead of
        // messing around we'll just define it for now.
        // 
        // Some ARM64 processors have larger cache line sizes. Intel processors will fetch
        // two cache lines at a time when loads are aligned on even cache lines, so the
        // "hardware destructive interference size" should arguably be 128 since they will
        // interfere with each other in terms of evicting cache lines but our goal here is
        // to have a "chunky" size that we "know" is going to be in very fast memory that
        // should be accessed as a unit before moving on to the next.
        //
        constexpr std::size_t cache_line_length = 64;

        using cache_line_t = std::array<char8_t, cache_line_length>;

        struct break_parse_context
        {
            constexpr break_parse_context() noexcept:
                m_position{0}, m_in_quote(false), m_last_char(0), m_done(false)
            {}

            m::io::position_t m_position;
            bool              m_in_quote;
            bool              m_done;
            char              m_last_char;
        };

        template <typename Function, typename... Args>
        std::size_t
        find_breaks(break_parse_context& context,
                    cache_line_t const&  cache_line,
                    Function             f,
                    Args... args)
        {
            std::size_t       len{};
            bool              in_quote  = context.m_in_quote;
            bool              done      = context.m_done;
            char              last_char = context.m_last_char;
            m::io::position_t position  = context.m_position;

            // done shouldn't happen!

            if (done)
                throw std::runtime_error("attempting to break after done!");

            for (char8_t const b: cache_line)
            {
                if (b == 0)
                {
                    context.m_in_quote  = in_quote;
                    context.m_last_char = last_char;
                    context.m_done      = true;
                    return len;
                }

                len++;
                ++position;

                char ch = static_cast<char>(b);

                switch (ch)
                {
                    case '"':
                    {
                        if (last_char == '"')
                        {
                            //
                            // If we thought the quote had gotten us in to or out of a quote, NOPE!
                            //
                            in_quote = !in_quote;
                        }

                        break;
                    }

                    case '\r':
                    {
                        if (!in_quote)
                        {
                            //
                            // There's a design decision here about what to
                            // do about sequences like /n/r andn /r/n/r.
                            // We're going to treat /r/n as a single line
                            // break otherwise these sequences are multiple.
                            // Each carriage return is considered a break.
                            //
                            std::invoke(std::forward<Function>(f),
                                        break_reason::row,
                                        std::forward<Args>(args)...);
                        }
                        break;
                    }

                    case '\n':
                    {
                        if (!in_quote)
                        {
                            //
                        }
                        break;
                    }

                    case ',':
                    {
                        break;
                    }
                }
            }
        }

    } // namespace csv
} // namespace m

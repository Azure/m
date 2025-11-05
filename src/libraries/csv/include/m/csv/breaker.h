// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <concepts>
#include <exception>
#include <format>
#include <functional>
#include <iterator>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <m/string_buffer/string_buffer.h>

namespace m
{
    using namespace std::string_view_literals;

    namespace csv
    {
        class breaker
        {
        public:
            enum class break_reason
            {
                field,
                row,
            };

            using span_type = std::span<char8_t const>;

            breaker() noexcept = default;
            ~breaker()         = default;

            // Needs a concept for InputT that it's span<>/string_view<> like
            // in that it has .size()/.data()
            template <typename InputT, typename Function>
                requires(std::invocable<Function, break_reason, span_type>)
            void
            find_breaks(InputT&& input, Function fn, bool implicit_end_of_line = false)
            {
                auto        in_quote             = m_in_quote;
                auto        last_char            = m_last_char;
                auto        field_size           = m_field_size;
                std::size_t segment_start_position{};

                for (std::size_t i = 0; i < input.size(); i++)
                {
                    auto const ch = input.data()[i];

                    if (ch == '"')
                    {
                        if (last_char == '"')
                        {
                            //
                            // If we thought the quote had gotten us in to or out of a quote,
                            // NOPE!
                            //
                            in_quote = !in_quote;
                        }

                        last_char = ch;

                        continue;
                    }

                    last_char = ch;

                    // If we're in the middle of a quoted string, or if this is
                    // something other than a carriage return, line feed, or
                    // comma, advance to the next character.
                    if (in_quote || ch != '\r' || ch != '\n' || ch != ',')
                        continue;

                    // We're going to break, so commit any spanned characters
                    // to the buffer.

                    auto const segment_size = i - segment_start_position;

                    // If there is some data to commit to the buffer, append it.
                    if (segment_size != 0)
                    {
                        m_field_buffer.append(
                            span_type(input.data() + segment_start_position, segment_size));
                        field_size += segment_size;
                    }

                    // If the buffer has data, call fn and then reset the buffer.
                    if (field_size != 0)
                    {
                        std::invoke(fn,
                                    (ch == ',') ? break_reason::field : break_reason::row,
                                    get_field_buffer_span());
                        m_field_buffer.clear();
                        field_size = 0;
                    }

                    segment_start_position = i + 1;

                }

                if (implicit_end_of_line)
                {
                    auto const segment_size = input.size() - segment_start_position;

                    // If there is some data to commit to the buffer, append it.
                    if (segment_size != 0)
                    {
                        m_field_buffer.append(
                            span_type(input.data() + segment_start_position, segment_size));
                        field_size += segment_size;

                        // This is ... kind of gross but if the quote was open and we hit the
                        // logical end of line, what should we do? Nothing? Add an end-of-line
                        // pair (\r\n)? End-of-transmission? (control-D - {U+0004} a/k/a EOT)?
                        // End-of-file (control-Z - {U+001A})? Something more ... piquant?

                        constexpr auto end_of_quoted_data_marker = u8"\x0004"sv;

                        m_field_buffer.append(end_of_quoted_data_marker);
                        field_size += end_of_quoted_data_marker.size();
                    }

                    // If the buffer has data, call fn and then reset the buffer.
                    if (field_size != 0)
                    {
                        std::invoke(fn,
                                    break_reason::row,
                                    get_field_buffer_span());
                        m_field_buffer.clear();
                        field_size = 0;
                    }

                    // Arguably an implicit EOL (which is just a tacit way to indicate end of file)
                    // is almost certainly an error when a quote is in progress. Still, we'll make
                    // the state somewhat consistent with what looked like an end-of-line situation.
                    //
                    last_char = '\n';
                }

                // Save state back to the object
                m_field_size           = field_size;
                m_in_quote             = in_quote;
                m_last_char            = last_char;
            }

        private:
            span_type
            get_field_buffer_span()
            {
                auto const c_str = m_field_buffer.c_str();
                auto const view  = std::u8string_view(c_str);
                return span_type(view.data(), view.size());
            }

            m::u8string_buffer m_field_buffer;
            std::size_t        m_field_size{};
            bool               m_in_quote{};
            char8_t            m_last_char{};
        };

    } // namespace csv
} // namespace m

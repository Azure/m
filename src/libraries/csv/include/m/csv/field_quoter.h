// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <m/error_handling/macros.h>
#include <m/utf/transcode.h>

using namespace std::string_view_literals;

//
//
// TODO: Like everywhere else, we lack proper Utf-8 and Utf-16 decoding here, so there is
// a lack of proper handling of surrogate pairs.
//

namespace m
{
    namespace csv
    {
        struct field_quoter
        {
            static inline constexpr auto characters_that_require_quotes = ",\r\n\""sv;

            template <typename InputIt, typename SentinelT, typename OutIt>
                requires(std::forward_iterator<InputIt> &&
                         std::sized_sentinel_for<SentinelT, InputIt> &&
                         // std::output_iterator<char8_t, OutIt>
                         std::weakly_incrementable<OutIt> &&
                         std::indirectly_writable<OutIt, char8_t>)
            static OutIt
            enquote(InputIt start, SentinelT end, OutIt outit)
            {
                bool quote       = false;
                using value_type = typename std::iterator_traits<InputIt>::value_type;

                if (start != end)
                {
                    auto it = start;

                    for (;;)
                    {
                        auto ch = *it++;
                        if ((ch < 32) ||  // space
                            (ch > 126) || // end of printable ASCII
                            (ch == ',') || (ch == '"') || (ch == '{'))
                        {
                            quote = true;
                            break;
                        }

                        if (it == end)
                            break;
                    }
                }

                if (!quote)
                {
                    if constexpr (std::is_same_v<value_type, char8_t>)
                    {
                        return std::copy(start, end, outit);
                    }
                    else
                    {
                        return utf::transcode<char8_t>(start, end, outit);
                    }
                }

                if (quote)
                    *outit++ = '"';

                auto it = start;

                while (it != end)
                {
                    auto ch = *it++;

                    if ((ch != '\r' && ch != '\n') && ((ch < 32) || (ch > 126) || (ch == '{')))
                    {
                        //
                        // Non-printable characters other than CR and LF are
                        // mapped to {U+xxxx}
                        //
                        // Open braces are mapped to {U+007b}. Sorry.
                        //

                        // We assume that the characters we're dealing with are not char32_t.
                        static_assert(sizeof(ch) <= 2);
                        outit =
                            std::format_to(outit, "{{U+{:04x}}}", static_cast<uint_least16_t>(ch));
                    }
                    else if (ch == '"')
                    {
                        M_INTERNAL_ERROR_CHECK(quote);
                        *outit++ = '"';
                        *outit++ = '"';
                    }
                    else
                    {
                        *outit++ = static_cast<char8_t>(ch);
                    }
                }

                if (quote)
                    *outit++ = '"';

                return outit;
            }

            template <typename StringishT, typename OutIt>
                requires(std::indirectly_writable<OutIt, char8_t>

                         //            std::output_iterator<char8_t, OutIt>
                         )
            static OutIt
            enquote(StringishT const& input, OutIt outit)
            {
                return enquote(input.begin(), input.end(), outit);
            }

            template <typename StringishT, typename OutIt>
                requires(std::indirectly_writable<OutIt, char8_t>
                         // std::output_iterator<char8_t, OutIt>
                         )
            static OutIt
            enquote(StringishT&& input, OutIt outit)
            {
                return enquote(input.begin(), input.end(), outit);
            }
        };
    } // namespace csv
} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <exception>
#include <format>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

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

            template <typename OutputBackIterT, typename StringishT>
            static OutputBackIterT
            enquote(OutputBackIterT iter, StringishT&& input)
            {
                bool must_quote = false;

                for (auto const ch: input)
                {
                    if ((ch < 32) ||  // space
                        (ch > 126) || // end of printable ASCII
                        (ch == ',') || (ch == '"') || (ch == '{'))
                    {
                        must_quote = true;
                        break;
                    }
                }

                if (!must_quote)
                {
                    //
                    // Fast path
                    //
                    iter = std::copy(input.begin(), input.end(), iter);
                }
                else
                {
                    *iter++ = '"';

                    std::ranges::for_each(input, [&](auto ch) {
                        if ((ch != '\r' && ch != '\n') && ((ch < 32) || (ch > 126) || (ch == '{')))
                        {
                            //
                            // Non-printable characters other than CR and LF are
                            // mapped to {U+xxxx}
                            //
                            // Open braces are mapped to {U+007b}. Sorry.
                            //

                            // We assume that the characters we're dealing with are not char32_t.
                            static_assert(sizeof(ch) <= 4);
                            iter = std::format_to(iter, "{{U+{:04x}}}", static_cast<uint16_t>(ch));
                        }
                        else if (ch == '"')
                        {
                            *iter++ = '"';
                            *iter++ = '"';
                        }
                        else
                        {
                            *iter++ = ch;
                        }
                    });

                    *iter++ = '"';
                }

                return iter;
            }
        };
    } // namespace csv
} // namespace m

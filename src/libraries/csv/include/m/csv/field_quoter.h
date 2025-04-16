// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <exception>
#include <format>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include <gsl/gsl>

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
        template <typename OutputBackIterT>
        struct field_quoter
        {
            constexpr field_quoter(OutputBackIterT& iter) noexcept: m_iter(iter) {}

            static inline constexpr std::wstring_view characters_that_require_quotes = L",\r\n\""sv;

            void
            enquote(std::wstring_view input)
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
                    // Fast path:
                    //
                    std::ranges::for_each(input, [&](wchar_t ch) {
                        *m_iter = ch;
                        ++m_iter;
                    });
                }
                else
                {
                    *m_iter = L'"';
                    ++m_iter;

                    std::ranges::for_each(input, [&](wchar_t ch) {
                        if ((ch != '\r' && ch != '\n') && ((ch < 32) || (ch > 126) || (ch == '{')))
                        {
                            //
                            // Non-printable characters other than CR and LF are
                            // mapped to {U+xxxx}
                            //
                            // Open braces are mapped to {U+007b}. Sorry.
                            // 
                            m_iter = std::format_to(m_iter, L"{{U+{:04x}}}", ch);
                        }
                        else if (ch == L'"')
                        {
                            *m_iter = L'"';
                            ++m_iter;

                            *m_iter = L'"';
                            ++m_iter;
                        }
                        else
                        {
                            *m_iter = ch;
                            ++m_iter;
                        }
                    });

                    *m_iter = L'"';
                    ++m_iter;
                }
            }

            OutputBackIterT& m_iter;
        };

        template <typename T>
        field_quoter(T) -> field_quoter<T>;

    } // namespace csv
} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <exception>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>

#include "field_quoter.h"

using namespace std::string_view_literals;

namespace m
{
    namespace csv
    {
        struct writer_traits
        {
            static inline constexpr std::wstring_view line_break = L"\r\n"sv;
        };

        template <typename OutputBackIterT, typename TraitsT = writer_traits>
        struct writer
        {
            using output_back_iter_t = OutputBackIterT;
            using traits_t           = TraitsT;

            constexpr writer(output_back_iter_t& iter) noexcept:
                m_iter(iter), m_field_quoter(iter), m_first_field{true}, m_first_row{true}
            {}

            template <typename T>
            void
            write_row(T const& row)
            {
                if (m_first_row)
                    m_first_row = false;
                else
                {
                    std::ranges::for_each(traits_t::line_break, [this](auto ch) {
                        *m_iter = ch;
                        ++m_iter;
                    });
                }

                std::ranges::for_each(row, [this](std::wstring_view str) { write_field(str); });

                m_first_field = true;
            }

            void
            write_end()
            {
                std::ranges::for_each(traits_t::line_break, [this](auto ch) {
                    *m_iter = ch;
                    ++m_iter;
                });
            }

        protected:
            void
            write_field(std::wstring_view input)
            {
                if (m_first_field)
                    m_first_field = false;
                else
                {
                    *m_iter = L',';
                    ++m_iter;
                }

                m_field_quoter.enquote(input);
            }

            output_back_iter_t&              m_iter;
            bool                             m_first_field;
            bool                             m_first_row;
            field_quoter<output_back_iter_t> m_field_quoter;
        };

        template <typename T>
        writer(T) -> writer<T>;
    } // namespace csv
} // namespace m

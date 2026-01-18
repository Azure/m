// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <concepts>
#include <exception>
#include <functional>
#include <iterator>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <m/string_buffer/string_buffer.h>

#include "field_quoter.h"

namespace m
{
    using namespace std::string_view_literals;

    namespace csv
    {
        namespace csv_impl
        {
            static inline m::u8string_buffer unused;
            using back_inserter_type = decltype(std::back_inserter(unused));
        } // namespace csv_impl

        struct writer_traits
        {
            static inline constexpr bool write_line_breaks = true;
            static inline constexpr auto line_break        = "\r\n"sv;
        };

        template <typename LineWriterT, typename TraitsT = writer_traits>
            requires(std::invocable<LineWriterT, std::span<char8_t const>>)
        struct writer
        {
            using line_writer_type   = LineWriterT;
            using traits_t           = TraitsT;
            using string_buffer_type = u8string_buffer;

            constexpr writer(line_writer_type line_writer) noexcept:
                m_line_writer(line_writer), m_first_field{true}
            {}

            template <typename T>
            void
            write_row(T&& row)
            {
                auto outit    = std::back_inserter(m_line_buffer);
                m_first_field = true;
                m_line_buffer.clear();
                std::ranges::for_each(std::forward<T>(row), [this, &outit](auto const& str) {
                    // Explicit use of `this` because clang gives a warning
                    // for the explicit capture of `this` because it does not
                    // see it used using the toolset on ubuntu-latest on
                    // github, 10/30/2025.
                    this->write_field(str, outit);
                });
                outit = std::ranges::copy(traits_t::line_break, outit).out;

                m_line_buffer.for_each_span([this](auto spn) { std::invoke(m_line_writer, spn); });

                std::invoke(m_line_writer, string_buffer_type::span_type());
            }

            template <typename T>
            void
            write_row(T const& row)
            {
                auto outit    = std::back_inserter(m_line_buffer);
                m_first_field = true;
                m_line_buffer.clear();
                std::ranges::for_each(row, [this, &outit](auto const& str) {
                    // Explicit use of `this` because clang gives a warning
                    // for the explicit capture of `this` because it does not
                    // see it used using the toolset on ubuntu-latest on
                    // github, 10/30/2025.
                    outit = this->write_field(str, outit);
                });
                outit = std::ranges::copy(traits_t::line_break, outit).out;

                m_line_buffer.for_each_span([this](auto spn) { std::invoke(m_line_writer, spn); });

                std::invoke(m_line_writer, string_buffer_type::span_type());
            }

        protected:
            template <typename StringishT, typename IteratorT>
            IteratorT
            write_field(StringishT&& input, IteratorT outit)
            {
                if (m_first_field)
                    m_first_field = false;
                else
                    *outit++ = ',';

                return field_quoter::enquote(std::forward<StringishT>(input), outit);
            }

            template <typename StringishT, typename IteratorT>
            IteratorT
            write_field(StringishT const& input, IteratorT outit)
            {
                if (m_first_field)
                    m_first_field = false;
                else
                    *outit++ = ',';

                return field_quoter::enquote(input, outit);
            }

            line_writer_type   m_line_writer;
            string_buffer_type m_line_buffer;
            bool               m_first_field;
        };

        template <typename T>
        writer(T) -> writer<T>;
    } // namespace csv
} // namespace m

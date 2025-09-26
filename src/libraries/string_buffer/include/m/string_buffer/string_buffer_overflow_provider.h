// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <version>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/inplace_vector/inplace_vector.h>
#include <m/utility/pointers.h>
#include <m/utility/smallest_size.h>

namespace m
{
    template <typename CharT>
    struct basic_string_buffer_overflow_provider
    {
        using value_type = CharT;
        using span_type  = std::span<value_type const>;

        basic_string_buffer_overflow_provider() = default;

        // Appends what it can. Updates spn to refer to what is left to append.
        void
        append(span_type& spn)
        {
#if __cpp_lib_containers_ranges
            m_vector.append_range(spn);
#else
            m_vector.reserve(m_vector.size() + spn.size());

            for (auto const& e: spn)
                m_vector.emplace_back(e);
#endif

            spn = span_type();
        }

        void
        assign(span_type spn)
        {
            m_vector.assign(spn.begin(), spn.end());
        }

        void
        clear()
        {
            m_vector.clear();
        }

        template <typename IteratorT>
        IteratorT
        copy_to(IteratorT outit)
        {
            auto [in, out] = std::ranges::copy(m_vector, outit);
            return out;
        }

        std::size_t
        size() const noexcept
        {
            return m_vector.size();
        }

        void
        push_back(value_type const& v)
        {
            m_vector.push_back(v);
        }

        void
        push_back(value_type&& v)
        {
            m_vector.push_back(std::move(v));
        }

        span_type
        span() const
        {
            return span_type(m_vector.data(), m_vector.size());
        }

    private:
        std::vector<CharT> m_vector;
    };
} // namespace m

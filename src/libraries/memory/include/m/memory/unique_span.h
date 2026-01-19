// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <limits>
#include <malloc.h>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

#include <m/byte_streams/byte_streams.h>
#include <m/error_handling/macros.h>
#include <m/math/math.h>
#include <m/memory/raw_array_allocator.h>

namespace m
{
    template <typename T>
    class unique_span
    {
    public:
        using span_type        = std::span<T, std::dynamic_extent>;
        using element_type     = typename span_type::element_type;
        using value_type       = typename span_type::value_type;
        using const_value_type = std::add_const_t<value_type>;
        using size_type        = typename span_type::size_type;
        using difference_type  = typename span_type::difference_type;
        using pointer          = typename span_type::pointer;
        using const_pointer    = typename span_type::const_pointer;
        using reference        = typename span_type::reference;
        using const_reference  = typename span_type::const_reference;
        using iterator         = typename span_type::iterator;
        using reverse_iterator = typename span_type::reverse_iterator;

#ifdef M_HAS_CXX23
        using const_iterator         = typename span_type::const_iterator;
        using const_reverse_iterator = typename span_type::const_reverse_iterator;
#endif

        constexpr unique_span() noexcept: m_span() {}

        constexpr unique_span(unique_span&& other): m_span()
        {
            using std::swap;
            swap(m_span, other.m_span);
        }

        //
        // One could imagine copy operations but not at this time.
        //
        unique_span(unique_span const&) = delete;

        unique_span(size_type n): m_span()
        {
            m::raw_array_allocator<value_type> ra(n);
            ra.default_construct();
            m_span = ra.release();
        }

        template <typename Fn>
        unique_span(size_type n, Fn&& fn): m_span()
        {
            m::raw_array_allocator<value_type> ra(n);

            for (size_type i = 0; i < n; i++)
                std::invoke(fn, i, ra[i]);

            m_span = ra.release();
        }

        /// <summary>
        /// Constructs the unique_span based on the std::initializer_list
        /// passed in.
        ///
        /// NOTE: this implementation currently default-constructs the data
        /// array, and then copies the data on top of it.
        /// </summary>
        /// <param name="il"></param>
        unique_span(std::initializer_list<T> il): m_span()
        {
            m::raw_array_allocator<value_type> ra(il.size());

            std::uninitialized_copy_n(il.begin(), il.size(), ra.get());

            m_span = ra.release();
        }

        /// <summary>
        /// Constructor that permits initialization from spans with static
        /// extent as well as dynamic
        ///
        /// NOTE: this implementation currently default-constructs the data
        /// array, and then copies the data on top of it.
        /// </summary>
        /// <typeparam name="N">Not used except to match the span `s`'s type.</typeparam>
        /// <param name="s"></param>
        template <std::size_t N>
        unique_span(std::span<T, N> s): m_span()
        {
            m::raw_array_allocator<value_type> ra(s.size());

            std::uninitialized_copy_n(s.begin(), s.size(), ra.get());

            m_span = ra.release();
        }

        template <typename IteratorT, typename EndIteratorT>
            requires(std::random_access_iterator<IteratorT>)
        unique_span(IteratorT it, EndIteratorT end)
        {
            auto                               size = std::distance(it, end);
            m::raw_array_allocator<value_type> ra(size);
            std::uninitialized_copy(it, end, ra.get());
            m_span = ra.release();
        }

        template <typename RangeT>
        unique_span(RangeT&& r)
        {
            auto                               size = static_cast<size_type>(std::ranges::size(r));
            m::raw_array_allocator<value_type> ra(size);
            std::ranges::uninitialized_copy(r, ra);
            m_span = ra.release();
        }

        ~unique_span() { reset(); }

        void
        swap(unique_span&& other) noexcept
        {
            using std::swap;
            swap(m_span, other.m_span);
        }

        unique_span&
        operator=(unique_span&& other) noexcept
        {
            using std::swap;
            swap(m_span, other.m_span);
            return *this;
        }

        //
        // One could imagine copy operations but not at this time.
        //
        unique_span&
        operator=(unique_span const& other) = delete;

        void
        reset()
        {
            auto const s = std::exchange(m_span, std::span<value_type, std::dynamic_extent>());

            if (auto const ptr = s.data(); ptr != nullptr)
            {
                std::ranges::destroy(s);

                // Have raw_array_allocator take control over the allocation again
                // so that it can deallocate it, however it had allocated
                // it.
                raw_array_allocator ra(ptr, s.size());
            }
        }

        constexpr
        operator std::span<T>() const noexcept
        {
            return m_span;
        }

        constexpr std::span<T>
        span() const noexcept
        {
            return m_span;
        }

        /// <summary>
        /// Introduce implicit conversion to std::span<T const> if
        /// T was not const.
        /// </summary>
        constexpr
        operator std::span<const_value_type>() const noexcept
            requires(!std::is_same_v<element_type, const_value_type>)
        {
            return std::span<const_value_type, std::dynamic_extent>(
                const_cast<const_value_type*>(m_span.data()), m_span.size());
        }

        constexpr reference
        operator[](size_type i) const noexcept
        {
            return m_span.data()[i];
        }

        constexpr reference
        at(size_type i) const
        {
            if (i >= m_span.size())
                throw std::out_of_range("i");

            return m_span.data()[i];
        }

        constexpr pointer
        data() const noexcept
        {
            return m_span.data();
        }

        constexpr size_type
        size() const noexcept
        {
            return m_span.size();
        }

        auto
        cbegin() const
        {
            return m_span.cbegin();
        }

        auto
        cend() const
        {
            return m_span.cend();
        }

        auto
        begin() const
        {
            return m_span.cbegin();
        }

        auto
        end() const
        {
            return m_span.cend();
        }

    private:
        std::span<value_type> m_span{};
    };

    template <typename T>
    unique_span(T&&) -> unique_span<std::remove_reference_t<std::ranges::range_reference_t<T>>>;
} // namespace m

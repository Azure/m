// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <type_traits>

namespace m
{
    template <typename T>
    class unique_span
    {
    public:
        using element_type           = T;
        using value_type             = std::remove_cvref_t<T>;
        using const_value_type       = std::add_const_t<value_type>;
        using size_type              = std::size_t;
        using difference_type        = std::ptrdiff_t;
        using pointer                = T*;
        using const_pointer          = T const*;
        using reference              = T&;
        using const_reference        = T const&;
        using span_type              = std::span<T, std::dynamic_extent>;
        using iterator               = typename span_type::iterator;
        using const_iterator         = typename span_type::const_iterator;
        using reverse_iterator       = typename span_type::reverse_iterator;
        using const_reverse_iterator = typename span_type::const_reverse_iterator;

        constexpr unique_span() noexcept: m_span() {}

        constexpr unique_span(unique_span&& other): m_span()
        {
            using std::swap;

            swap(m_span, other.m_span);
        }

        unique_span(std::size_t n): m_span(new T[n], n) {}

        template <typename Fn>
        unique_span(std::size_t n, Fn&& fn): m_span(new T[n], n)
        {
            for (std::size_t i = 0; i < n; i++)
                std::invoke(fn, i, std::ref(m_span.data()[i]));
        }

        /// <summary>
        /// Constructs the unique_span based on the std::initializer_list
        /// passed in.
        ///
        /// NOTE: this implementation currently default-constructs the data
        /// array, and then copies the data on top of it.
        /// </summary>
        /// <param name="il"></param>
        unique_span(std::initializer_list<T> il): m_span(new T[il.size()], il.size())
        {
            std::copy_n(il.begin(), il.size(), m_span.begin());
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
        unique_span(std::span<T, N> s): m_span(new T[s.size()], s.size())
        {
            std::copy_n(s.begin(), s.size(), m_span.data());
        }

        ~unique_span() { delete[] m_span.data(); }

        constexpr
        operator std::span<T>() const noexcept
        {
            return m_span;
        }

        /// <summary>
        /// Introduce implicit conversion to std::span<T const> if
        /// T was not const.
        /// </summary>
        constexpr
            operator std::span<const_value_type>() const noexcept
            requires !std::is_same_v<element_type, const_value_type>
        {
            return std::span<const_value_type, std::dynamic_extent>(
                const_cast<const_value_type*>(m_span.data()), m_span.size());
        }

        constexpr T*
        data() const noexcept
        {
            return m_span.data();
        }

        constexpr std::size_t
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
        std::span<T> m_span;
    };

} // namespace m
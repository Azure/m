// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <compare>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <concepts>
#include <deque>
#include <iterator>
#include <optional>

namespace mc
{
    template <typename UnderlyingIteratorT, class MakerT>
    class random_access_wrapper_iterator
    {
        using underlying_iterator = UnderlyingIteratorT;
        using maker               = MakerT;

    public:
        using iterator_category = std::random_access_iterator_tag;

        using difference_type = typename underlying_iterator::difference_type;
        using value_type      = typename underlying_iterator::value_type;

        random_access_wrapper_iterator() = default;
        random_access_wrapper_iterator(random_access_wrapper_iterator const& other):
            m_it(other.m_it)
        {}
        random_access_wrapper_iterator(random_access_wrapper_iterator&& other) noexcept
        {
            using std::swap;
            swap(m_it, other.m_it);
        }
        ~random_access_wrapper_iterator() = default;

        random_access_wrapper_iterator&
        operator=(random_access_wrapper_iterator const& other)
        {
            m_it = other.m_it;
            return *this;
        }

        random_access_wrapper_iterator&
        operator=(random_access_wrapper_iterator&& other) noexcept
        {
            using std::swap;
            swap(m_it, other.m_it);
            return *this;
        }

        random_access_wrapper_iterator&
        operator++()
        {
            ++m_it;
            return *this;
        }

        random_access_wrapper_iterator
        operator++(int)
        {
            random_access_wrapper_iterator old = *this;
            operator++();
            return old;
        }

        random_access_wrapper_iterator&
        operator--()
        {
            --m_it;
            return *this;
        }

        random_access_wrapper_iterator
        operator--(int)
        {
            random_access_wrapper_iterator old = *this;
            operator--();
            return old;
        }

        random_access_wrapper_iterator
        operator+(difference_type n) const
        {
            underlying_iterator it = m_it + n;
            return random_access_wrapper_iterator(it);
        }

        friend random_access_wrapper_iterator
        operator+(difference_type n, random_access_wrapper_iterator x)
        {
            underlying_iterator it = n + x.m_it;
            return random_access_wrapper_iterator(it);
        }

        difference_type
        operator-(random_access_wrapper_iterator const& x) const
        {
            return m_it - x.m_it;
        }

        random_access_wrapper_iterator
        operator-(difference_type n) const
        {
            underlying_iterator it = m_it - n;
            return random_access_wrapper_iterator(it);
        }

        friend random_access_wrapper_iterator
        operator-(difference_type n, random_access_wrapper_iterator x)
        {
            underlying_iterator it = n - x.m_it;
            return random_access_wrapper_iterator(it);
        }

        random_access_wrapper_iterator&
        operator+=(difference_type n)
        {
            m_it += n;
            return *this;
        }

        random_access_wrapper_iterator&
        operator-=(difference_type n)
        {
            m_it -= n;
            return *this;
        }

        value_type&
        operator[](difference_type n) const
        {
            return m_it[n];
        }

        value_type&
        operator*() const
        {
            return *m_it;
        }

        bool
        operator==(random_access_wrapper_iterator const& x) const
        {
            return m_it == x.m_it;
        }

        decltype(auto)
        operator<=>(random_access_wrapper_iterator const& x) const
        {
            return m_it <=> x.m_it;
        }

    protected:
        random_access_wrapper_iterator(underlying_iterator it): m_it(std::move(it)) {}

        underlying_iterator m_it;

        // Only maker may invoke the constructor
        friend maker;
    };

    namespace iterators_impl
    {
        struct verify_it
        {
            static_assert(
                std::random_access_iterator<
                    random_access_wrapper_iterator<typename std::deque<int>::iterator, verify_it>>);
        };
    } // namespace iterators_impl
} // namespace mc

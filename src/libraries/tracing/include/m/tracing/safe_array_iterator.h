// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
#include <initializer_list>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace m
{
    namespace tracing
    {
        template <typename T>
        struct safe_array_iterator
        {
            using iterator_category = std::output_iterator_tag;
            using value_type        = typename T::value_type;
            using difference_type   = ptrdiff_t;
            using pointer           = void;
            using reference         = void;
            using container_type    = T;
            using iterator_traits   = std::iterator_traits<safe_array_iterator<T>>;

            safe_array_iterator(T& arr, std::size_t index) noexcept: m_array(arr), m_index(index) {}

            safe_array_iterator(safe_array_iterator const& other) noexcept:
                m_array(other.m_array), m_index(other.m_index)
            {}

            safe_array_iterator&
            operator=(safe_array_iterator const& other)
            {
                if (&m_array != &other.m_array)
                    throw std::runtime_error("can't assign between these iterators");

                m_index = other.m_index;
                return *this;
            }

            value_type&
            operator*()
            {
                if (m_index < m_array.size())
                    return m_array[m_index];
                return m_parked_value;
            }

            safe_array_iterator&
            operator++()
            {
                if (m_index < m_array.size())
                    m_index++;

                return *this;
            }

            safe_array_iterator
            operator++(int)
            {
                safe_array_iterator old = *this;
                operator++();
                return old;
            }

            T&          m_array;
            std::size_t m_index;
            value_type  m_parked_value{};
        };
    } // namespace tracing
} // namespace m

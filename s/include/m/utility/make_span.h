// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

namespace m
{
    template <typename T>
    constexpr std::span<T, std::dynamic_extent>
    make_span(T* p, std::size_t len)
    {
        return std::span<T, std::dynamic_extent>(p, len);
    }

    template <class T>
    constexpr std::span<T, std::dynamic_extent>
    make_span(T* first, T* last)
    {
        return std::span<T, std::dynamic_extent>(first, last);
    }

    template <class T, std::size_t N>
    constexpr std::span<T, N>
    make_span(T (&arr)[N]) noexcept
    {
        return span<T, N>(arr);
    }

    template <class Container>
    constexpr std::span<typename Container::value_type, std::dynamic_extent>
    make_span(Container& cont)
    {
        return std::span<typename Container::value_type>(cont);
    }

    template <class Container>
    constexpr std::span<const typename Container::value_type>
    make_span(const Container& cont)
    {
        return std::span<const typename Container::value_type>(cont);
    }
} // namespace m

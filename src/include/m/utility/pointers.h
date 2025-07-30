// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <exception>
#include <stdexcept>
#include <type_traits>

#include <m/exception/exception.h>

namespace m
{
    template <typename T>
        requires std::is_pointer_v<T>
    class not_null
    {
    public:
        not_null() = delete;

        constexpr not_null(T v): m_v(v)
        {
            if (v == nullptr)
                throw m::invalid_parameter("v");
        }

        template <typename U>
            requires(std::is_base_of_v<std::remove_pointer_t<T>, U>)
        constexpr not_null(U* v): m_v(v)
        {
            if (v == nullptr)
                throw m::invalid_parameter("v");
        }

        constexpr not_null(not_null const& other) noexcept: m_v(other.m_v) {}

        template <typename U>
            requires(std::is_base_of_v<std::remove_pointer_t<T>, U>)
        constexpr not_null(not_null<U*> const& other) noexcept : m_v(other.m_v)
        {}

        not_null&
        operator=(not_null const& other)
        {
            m_v = other.m_v;
            return *this;
        }

        template <typename U>
            requires(std::is_base_of_v<std::remove_pointer_t<T>, U>)
        constexpr not_null& operator=(not_null<U*> const& other) noexcept
        {
            m_v = other.m_v;
            return *this;
        }

        static void
        swap(not_null& l, not_null& r) noexcept
        {
            T t{l.m_v};
            l.m_v = r.m_v;
            r.m_v = r;
        }

        ~not_null() = default;

        operator T() const noexcept { return m_v; }

        T
        operator->() const noexcept
        {
            return m_v;
        }

    private:
        T m_v;

        template <typename U>
            requires std::is_pointer_v<U>
        friend class not_null;
    };

    template <typename T>
        requires std::is_pointer_v<T>
    not_null(T) -> not_null<T>;

} // namespace m
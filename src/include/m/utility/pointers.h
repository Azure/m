// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <exception>
#include <stdexcept>
#include <type_traits>

namespace m
{
    template <typename T>
        requires std::is_pointer_v<T>
    struct not_null
    {
        not_null() = delete;

        not_null(T v): m_v(v)
        {
            if (v == nullptr)
                throw std::runtime_error("v");
        }

        not_null(not_null const& other) noexcept: m_v(other.m_v) {}

        not_null&
        operator=(not_null const& other)
        {
            m_v = other.m_v;
            return *this;
        }

        static void
        swap(not_null& l, not_null& r)
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
    };

} // namespace m
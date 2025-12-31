// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <cstddef>
#include <span>

#include <m/error_handling/macros.h>

namespace m
{
    //
    // byte_span and const_byte_span are introduced as the obvious
    // shortened names of the std based types.
    //
    using byte_span       = std::span<std::byte>;
    using const_byte_span = std::span<std::byte const>;

    //
    // Convert a byte_span to a span of Ts.
    //
    // Requires that the byte_span is precisely sized to be a multiple
    // of sizeof(T).
    //
    template <typename T>
    std::span<T const>
    as_ts(byte_span span)
    {
        M_VERIFY_PRECONDITION(span.size() % sizeof(T) == 0);
        return std::span<T const>(reinterpret_cast<T*>(span.data()), span.size() / sizeof(T));
    }

    template <typename T>
    std::span<T const>
    as_ts(const_byte_span span)
    {
        M_VERIFY_PRECONDITION(span.size() % sizeof(T) == 0);
        return std::span<T const>(reinterpret_cast<T const*>(span.data()), span.size() / sizeof(T));
    }

    template <typename T>
    std::span<T>
    as_writable_ts(byte_span span)
    {
        M_VERIFY_PRECONDITION(span.size() % sizeof(T) == 0);
        return std::span<T>(reinterpret_cast<T*>(span.data()), span.size() / sizeof(T));
    }

    //
    // Convert a byte_span to a span of Ts.
    //
    // Also returns the count of leftover bytes in an out reference parameter.
    //
    template <typename T>
    std::span<T const>
    as_ts(byte_span span, std::size_t& remainder)
    {
        remainder = span.size() % sizeof(T);
        return std::span<T const>(reinterpret_cast<T*>(span.data()), span.size() / sizeof(T));
    }

    template <typename T>
    std::span<T const>
    as_ts(const_byte_span span, std::size_t& remainder)
    {
        remainder = span.size() % sizeof(T);
        return std::span<T const>(reinterpret_cast<T const*>(span.data()), span.size() / sizeof(T));
    }

    template <typename T>
    std::span<T>
    as_writable_ts(byte_span span, std::size_t& remainder)
    {
        remainder = span.size() % sizeof(T);
        return std::span<T>(reinterpret_cast<T*>(span.data()), span.size() / sizeof(T));
    }

    template <typename T>
    std::span<T const>
    as_ts(byte_span span, byte_span& remainder)
    {
        auto const n               = span.size() / sizeof(T);
        auto const remainder_bytes = span.size() % sizeof(T);

        if (remainder_bytes != 0)
            remainder = span.subspan(span.size() - remainder_bytes);
        else
            remainder = byte_span{};

        return std::span<T const>(reinterpret_cast<T*>(span.data()), n);
    }

    template <typename T>
    std::span<T const>
    as_ts(const_byte_span span, const_byte_span& remainder)
    {
        auto const n               = span.size() / sizeof(T);
        auto const remainder_bytes = span.size() % sizeof(T);

        if (remainder_bytes != 0)
            remainder = span.subspan(span.size() - remainder_bytes);
        else
            remainder = const_byte_span{};

        return std::span<T const>(reinterpret_cast<T const*>(span.data()), n);
    }

    template <typename T>
    std::span<T>
    as_writable_ts(byte_span span, byte_span& remainder)
    {
        auto const n               = span.size() / sizeof(T);
        auto const remainder_bytes = span.size() % sizeof(T);

        if (remainder_bytes != 0)
            remainder = span.subspan(span.size() - remainder_bytes);
        else
            remainder = byte_span{};

        return std::span<T>(reinterpret_cast<T*>(span.data()), n);
    }

    template <typename T>
    std::span<T const>
    as_ts(byte_span span, std::size_t limit, byte_span& remainder)
    {
        auto const n               = (std::min)(limit, span.size() / sizeof(T));
        auto const remainder_bytes = span.size() - (n * sizeof(T));

        if (remainder_bytes != 0)
            remainder = span.subspan(span.size() - remainder_bytes);
        else
            remainder = byte_span{};

        return std::span<T const>(reinterpret_cast<T*>(span.data()), n);
    }

    template <typename T>
    std::span<T const>
    as_ts(const_byte_span span, std::size_t limit, const_byte_span& remainder)
    {
        auto const n               = (std::min)(limit, span.size() / sizeof(T));
        auto const remainder_bytes = span.size() - (n * sizeof(T));

        if (remainder_bytes != 0)
            remainder = span.subspan(span.size() - remainder_bytes);
        else
            remainder = const_byte_span{};

        return std::span<T const>(reinterpret_cast<T const*>(span.data()), n);
    }

    template <typename T>
    std::span<T>
    as_writable_ts(byte_span span, std::size_t limit, byte_span& remainder)
    {
        auto const n               = (std::min)(limit, span.size() / sizeof(T));
        auto const remainder_bytes = span.size() - (n * sizeof(T));

        if (remainder_bytes != 0)
            remainder = span.subspan(span.size() - remainder_bytes);
        else
            remainder = byte_span{};

        return std::span<T>(reinterpret_cast<T*>(span.data()), n);
    }

    //
    // Convert a byte_span to a span of Ts.
    //
    // Also returns the count of leftover bytes in an optional out pointer parameter.
    //
    // This allows the caller to not care if the remainder is returned and not define
    // storage for it.
    //
    template <typename T>
    std::span<T const>
    as_ts(byte_span span, std::size_t* remainder)
    {
        if (remainder != nullptr)
            *remainder = span.size() % sizeof(T);

        return std::span<T const>(reinterpret_cast<T*>(span.data()), span.size() / sizeof(T));
    }

    template <typename T>
    std::span<T const>
    as_ts(const_byte_span span, std::size_t* remainder)
    {
        if (remainder != nullptr)
            *remainder = span.size() % sizeof(T);

        return std::span<T const>(reinterpret_cast<T const*>(span.data()), span.size() / sizeof(T));
    }

    template <typename T>
    std::span<T>
    as_writable_ts(byte_span span, std::size_t* remainder)
    {
        if (remainder != nullptr)
            *remainder = span.size() % sizeof(T);

        return std::span<T>(reinterpret_cast<T*>(span.data()), span.size() / sizeof(T));
    }

    template <typename T>
    std::span<T const>
    as_ts(byte_span span, std::size_t limit, byte_span* remainder)
    {
        auto const n               = (std::min)(limit, span.size() / sizeof(T));
        auto const remainder_bytes = span.size() - (n * sizeof(T));

        if (remainder != nullptr)
        {
            if (remainder_bytes != 0)
                *remainder = span.subspan(span.size() - remainder_bytes);
            else
                *remainder = byte_span{};
        }

        return std::span<T const>(reinterpret_cast<T*>(span.data()), n);
    }

    template <typename T>
    std::span<T const>
    as_ts(const_byte_span span, std::size_t limit, const_byte_span* remainder)
    {
        auto const n               = (std::min)(limit, span.size() / sizeof(T));
        auto const remainder_bytes = span.size() - (n * sizeof(T));

        if (remainder != nullptr)
        {
            if (remainder_bytes != 0)
                *remainder = span.subspan(span.size() - remainder_bytes);
            else
                *remainder = const_byte_span{};
        }

        return std::span<T const>(reinterpret_cast<T const*>(span.data()), n);
    }

    template <typename T>
    std::span<T>
    as_writable_ts(byte_span span, std::size_t limit, byte_span* remainder)
    {
        auto const n               = (std::min)(limit, span.size() / sizeof(T));
        auto const remainder_bytes = span.size() - (n * sizeof(T));

        if (remainder != nullptr)
        {
            if (remainder_bytes != 0)
                *remainder = span.subspan(span.size() - remainder_bytes);
            else
                *remainder = byte_span{};
        }

        return std::span<T>(reinterpret_cast<T*>(span.data()), n);
    }
} // namespace m
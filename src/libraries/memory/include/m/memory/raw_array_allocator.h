// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <span>
#include <type_traits>

#include <m/error_handling/macros.h>
#include <m/math/math.h>
#include <m/memory/aligned_allocation.h>

namespace m
{
    template <typename T>
    class raw_array_allocator
    {
        /// <summary>
        /// Having more than max_count Ts specified will overflow std::size_t bytes.
        /// </summary>
        static inline constexpr auto max_count =
            (std::numeric_limits<std::size_t>::max)() / sizeof(T);

        //
        // Ensure that std::size_t and size_t are equivalent types.
        // numeric_limits::digits is the number of bits in the value and thus
        // is a very good representation, whereas assertions regarding just
        // the sizeof(T) can omit signs and padding.
        //
        static_assert(std::numeric_limits<size_t>::digits ==
                      std::numeric_limits<std::size_t>::digits);

    public:
        using value_type      = std::decay_t<T>;
        using pointer         = T*;
        using value_pointer   = value_type*;
        using reference       = T&;
        using value_reference = value_type&;
        using const_reference = T const&;
        using size_type       = std::size_t;
        using index_type      = std::size_t;
        using span_type       = std::span<value_type>;

        raw_array_allocator() = default;

        /// <summary>
        /// Constructs a raw_array_allocator object with a given pointer.
        ///
        /// Used almost exclusively to get a memory allocation that had
        /// previously been allocated via raw_array_allocator and then release()d
        /// to be again brought under the purview of raw_array_allocator and
        /// then deallocated.
        /// </summary>
        /// <param name="ptr">A pointer to the memory to be managed by the allocator.</param>
        constexpr explicit raw_array_allocator(T*        ptr,
                                               size_type size,
                                               bool      constructed = false) noexcept:
            m_span(ptr, size), m_constructed(constructed)
        {}

        raw_array_allocator(std::size_t const count): m_span{}, m_constructed{false}
        {
            if (count > max_count)
                throw std::bad_array_new_length();

            auto const bytes = count * sizeof(T);

            auto const allocated_span = aligned_alloc(std::align_val_t{alignof(T)}, bytes);
            m_span = as_writable_ts<value_type>(allocated_span, count, nullptr);
        }

        raw_array_allocator(raw_array_allocator const&) = delete;

        raw_array_allocator(raw_array_allocator&& other): m_span{}, m_constructed{}
        {
            using std::swap;

            swap(m_span, other.m_span);
            swap(m_constructed, other.m_constructed);
        }

        ~raw_array_allocator() { reset(); }

        void
        reset()
        {
            if (auto const s = std::exchange(m_span, span_type{}); s.data() != nullptr)
            {
                if (auto const constructed = std::exchange(m_constructed, false);
                    constructed)
                {
                    std::destroy(s.begin(), s.end());
                }

                aligned_free(std::as_writable_bytes(s));
            }
        }

        raw_array_allocator&
        operator=(raw_array_allocator&& other) noexcept
        {
            using std::swap;

            swap(m_span, other.m_span);
            swap(m_constructed, other.m_constructed);

            return *this;
        }

        value_reference
        operator[](index_type i)
        {
            return m_span[i];
        }

        const_reference
        operator[](index_type i) const
        {
            return m_span[i];
        }

        constexpr
        operator pointer() const
        {
            return m_span.data();
        }

        constexpr pointer
        get() const
        {
            return m_span.data();
        }

        span_type
        release()
        {
            auto const s  = std::exchange(m_span, span_type{});
            m_constructed = false;
            return s;
        }

        void
        operator=(raw_array_allocator const&) = delete;

        constexpr size_type
        size() const
        {
            return m_span.size();
        }

        constexpr bool
        constructed() const
        {
            return m_constructed;
        }

        value_type*
        begin() const
        {
            return m_span.data();
        }

        T const*
        cbegin() const
        {
            return m_span.data();
        }

        value_type*
        end() const
        {
            return m_span.data() + m_span.size();
        }

        T const*
        cend() const
        {
            return m_span.data() + m_span.size();
        }

        void
        default_construct()
        {
            // You may not attempt to default construct more than once
            if (m_constructed)
                throw std::runtime_error("Already constructed");

            std::uninitialized_default_construct(m_span.begin(), m_span.end());
            m_constructed = true;
        }

    private:
        span_type m_span{};
        bool      m_constructed{false};
    };
} // namespace m

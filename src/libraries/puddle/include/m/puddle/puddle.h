// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <m/bitset/bitset.h>
#include <m/utility/incrementer.h>
#include <m/utility/locked.h>
#include <m/utility/pointers.h>

namespace m
{
    /// <summary>
    /// The `puddle` class defines a puddle of allocated (but not constructed)
    /// objects from which the caller may allocate and release.
    ///
    /// It may or may not be faster than interacting with the heap,
    /// the intention is more around cache locality and perhaps constraining
    /// the growth of the number of objects.
    ///
    /// It is a building block for a `puddle`; for more complete information
    /// about the overall usage pattern, see the `puddle` type.
    ///
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <typeparam name="N"></typeparam>

    template <typename T, std::size_t N, typename Enable = void>
    class puddle;

    template <typename T, std::size_t N>
    class puddle<T, N, std::enable_if_t<N != 0>>
    {
        using this_type = puddle<T, N, void>;

        static inline constexpr std::size_t inline_item_count = N;

    public:
        using count_type = m::smallest_size_t<N>;

        puddle()              = default;
        puddle(puddle const&) = delete;
        puddle(puddle&& other) noexcept
        {
            using std::swap;

            swap(m_allocated, other.m_allocated);
            swap(m_in_use_bitset, other.m_in_use_bitset);
            swap(m_data, other.m_data);
        }

        ~puddle() = default;

        puddle&
        operator=(puddle const&) = delete;

        puddle&
        operator=(puddle&& other) noexcept
        {
            using std::swap;

            swap(m_allocated, other.m_allocated);
            swap(m_in_use_bitset, other.m_in_use_bitset);
            swap(m_data, other.m_data);

            return *this;
        }

        void
        swap(puddle& other) noexcept
        {
            using std::swap;

            swap(m_allocated, other.m_allocated);
            swap(m_in_use_bitset, other.m_in_use_bitset);
            swap(m_data, other.m_data);
        }

        struct allocation_result
        {
            T*         m_ptr;
            count_type m_slot;
        };

        std::optional<allocation_result>
        try_allocate()
        {
            if (m_allocated == m_in_use_bitset.size())
                return std::nullopt;

            M_INTERNAL_ERROR_CHECK(m_allocated < m_in_use_bitset.size());

            bitset_bit_allocator<N> ba(m_in_use_bitset);

            if (!ba.has_value())
                return std::nullopt;

            auto const slot         = ba.bit();
            auto const ptr          = reinterpret_cast<T*>(&m_data[sizeof(T) * slot]);
            auto       return_value = allocation_result{.m_ptr = ::new (ptr) T, .m_slot = static_cast<count_type>(slot)};
            m_allocated++;
            ba.release();

            return return_value;
        }

        /// <summary>
        /// Destroy the object in `ptr` corresponding to an allocated object at slot `slot`,
        /// releasing the slot for further consumption.
        /// </summary>
        /// <param name="ptr"></param>
        /// <param name="slot"></param>
        void
        deallocate(T* ptr, count_type slot)
        {
            M_INTERNAL_ERROR_CHECK(m_allocated > 0);
            M_INTERNAL_ERROR_CHECK(m_in_use_bitset.is_set(slot));
            M_INTERNAL_ERROR_CHECK(ptr == reinterpret_cast<T*>(&m_data[sizeof(T) * slot]));
            std::destroy_n(ptr, 1);
            release_internal(slot);
        }

        void
        release(count_type slot)
        {
            M_INTERNAL_ERROR_CHECK(m_allocated > 0);
            M_INTERNAL_ERROR_CHECK(m_in_use_bitset.is_set(slot));
            release_internal(slot);
        }

    private:
        void
        release_internal(count_type slot)
        {
            m_in_use_bitset.clear(slot);
            m_allocated--;
        }

        m::bitset<N> m_in_use_bitset;
        count_type   m_allocated{};
        alignas(T) std::array<std::byte, sizeof(T) * N> m_data;
    };

    template <typename T, std::size_t N>
    class puddle<T, N, std::enable_if_t<N == 0>>
    {
        using this_type = puddle<T, N, void>;

        static inline constexpr std::size_t inline_item_count = N;

    public:
        using count_type = m::smallest_size_t<N>;

        puddle()              = default;
        puddle(puddle const&) = delete;
        puddle(puddle&&)      = delete;
        ~puddle()             = default;

        puddle&
        operator=(puddle const&) = delete;

        puddle&
        operator=(puddle&&) = delete;

        struct allocation_result
        {
            T*         m_ptr;
            count_type m_slot;
        };

        std::optional<allocation_result>
        try_allocate()
        {
            return std::nullopt;
        }

        void
        deallocate(std::size_t)
        {
            M_NOT_IMPLEMENTED("No allocations were possible from a puddle of size zero!");
            //
        }
    };
} // namespace m

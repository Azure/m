// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <climits>
#include <cstdint>
#include <new>
#include <optional>
#include <ranges>
#include <type_traits>
#include <version>

#if M_HAS_MSVC
#include <intrin.h>
#endif

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/utility/smallest_size.h>

//
// With the MSVC compiler, there are compiler intrinsics for finding the
// first bit set in a chunk of storage, which implies that the unallocated
// state is "1"s. This may not be true of all compilers/platforms so we
// have to have a compilation option for this.
//

namespace m
{
    namespace bitset_impl
    {
        class bitset_base
        {
        public:
            //
            // Efficiency is a primary concern here; want to use mmx/sse types over time
            // so cache lines are a more interesting granularity than even plain integral
            // types.
            //
            // The greatest utility there will come when there is correspondance with
            // the lock-free atomic allocation capabilities which are clearly possible,
            // all platforms support such operations but for now, just something is needed
            // since there is no type provided by the standard library.
            //
            using representation_type = uint64_t;

            static inline constexpr std::size_t granularity =
                sizeof(representation_type) * CHAR_BIT;

            static inline constexpr std::size_t alignment =
#if __cpp_lib_hardware_interference_size
                std::hardware_constructive_interference_size
#else
                64
#endif
                ;

        protected:
        };
    } // namespace bitset_impl

    template <std::size_t N>
    class bitset : public bitset_impl::bitset_base
    {
        using bitset_base::granularity;
        using bitset_base::representation_type;

        static inline constexpr std::size_t rounded_N =
            (N + granularity - 1) - ((N + granularity - 1) % granularity);
        // cache line size can be assumed to be std::hardware_constructive_interference_size

        static_assert(rounded_N % granularity == 0);

        static inline constexpr std::size_t allocation_count = rounded_N / granularity;
        static inline constexpr std::size_t last_index       = allocation_count - 1;
        static inline constexpr std::size_t last_index_bits  = N % granularity;

    public:
        constexpr bitset() noexcept: m_bits{} { m_bits.fill(0); }

        constexpr std::size_t
        size() noexcept
        {
            return N;
        }

        constexpr std::size_t
        popcount() noexcept
        {
            return std::ranges::fold_left(
                m_bits, std::size_t{}, [](std::size_t acc, std::size_t rep) {
                    return acc + std::popcount(rep);
                });
        }

        constexpr std::optional<std::size_t>
        find_first_clear_and_set() noexcept
        {
            for (std::size_t i = 0; i < m_bits.size(); i++)
            {
                auto& rep = m_bits[i];

                auto const leading_unset = std::countr_one(rep);

                // If this is the last index in the array of representation
                // unsigned integers, we use "last_index_bits" as the maximal
                // bit count, otherwise the granularity.
                //

                auto const maxbit = (i == last_index) ? last_index_bits : granularity;

                if (leading_unset < maxbit)
                {
                    // we found a clear bit! claim it
                    rep = rep ^ (1ull << leading_unset);

                    return (i * granularity) + leading_unset;
                }
            }

            return std::nullopt;
        }

        constexpr std::optional<std::size_t>
        find_first_set_and_clear() noexcept
        {
            for (std::size_t i = 0; i < m_bits.size(); i++)
            {
                auto& rep = m_bits[i];

                auto const leading_set = std::countr_zero(rep);
                auto const maxbit      = (i == last_index) ? last_index_bits : granularity;

                if (leading_set < maxbit)
                {
                    // we found a set bit! claim it
                    rep = rep ^ (1ull << leading_set);

                    return (i * granularity) + leading_set;
                }
            }

            return std::nullopt;
        }

        constexpr void
        clear(std::size_t n) noexcept
        {
            precondition_validate_index(n);

            auto const storage_index = static_cast<std::size_t>(n / granularity);

            auto const bit_index = n % granularity;

            m_bits[storage_index] &= ~(1ull << bit_index);
        }

        constexpr void
        set(std::size_t n) noexcept
        {
            precondition_validate_index(n);

            auto const storage_index = static_cast<std::size_t>(n / granularity);

            auto const bit_index = n % granularity;

            m_bits[storage_index] |= (1ull << bit_index);
        }

        constexpr bool
        is_set(std::size_t n) noexcept
        {
            precondition_validate_index(n);

            auto const storage_index = static_cast<std::size_t>(n / granularity);

            auto const bit_index = n % granularity;

            representation_type rep = m_bits[storage_index] & (1ull << bit_index);
            return (rep != 0);
        }

    private:
        void
        precondition_validate_index(std::size_t n) noexcept
        {
            if (n >= size())
            {
#if M_HAS_MSVC
#pragma warning(push)
#pragma warning(disable : 4297)
#elif M_HAS_CLANG
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexceptions"
#else
#error Unsupported compiler
#endif

                throw m::runtime_error("Bit index out of range");

#if M_HAS_MSVC
#pragma warning(pop)
#elif M_HAS_CLANG
#pragma clang diagnostic pop
#else
#error Unsupported compiler
#endif
            }
        }

        alignas(bitset_base::alignment) std::array<uint64_t, allocation_count> m_bits;
    };

    template <std::size_t N>
    class bitset_bit_allocator
    {
    public:
        constexpr bitset_bit_allocator(bitset<N>& btset) noexcept: m_bitset(btset)
        {
            m_bit = m_bitset.find_first_clear_and_set();
        }

        bitset_bit_allocator(bitset_bit_allocator const&) = delete;
        constexpr bitset_bit_allocator(bitset_bit_allocator const&& other) noexcept:
            m_bitset(other.m_bitset)
        {
            using std::swap;

            swap(m_bit, other.m_bit);
        }

        constexpr bitset_bit_allocator&
        operator=(bitset_bit_allocator const&) = delete;

        constexpr bitset_bit_allocator
        operator=(bitset_bit_allocator&& other) noexcept
        {
            M_INTERNAL_ERROR_CHECK(&m_bitset == &other.m_bitset);

            using std::swap;
            swap(m_bit, other.m_bit);
            return *this;
        }

        ~bitset_bit_allocator() { reset(); }

        constexpr bool
        has_value() const noexcept
        {
            return m_bit.has_value();
        }

        constexpr std::size_t
        bit() const noexcept
        {
            M_INTERNAL_ERROR_CHECK(m_bit.has_value());
            return m_bit.value();
        }

        constexpr std::optional<std::size_t>
        release() noexcept
        {
            return std::exchange(m_bit, std::nullopt);
        }

        constexpr void
        reset(std::optional<std::size_t> bit = std::nullopt) noexcept
        {
            auto old = std::exchange(m_bit, bit);
            if (old.has_value())
                m_bitset.clear(old.value());
        }

    private:
        bitset<N>&                 m_bitset;
        std::optional<std::size_t> m_bit;
    };

} // namespace m

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

#include <m/cast/to.h>
#include <m/error_handling/macros.h>
#include <m/math/math.h>
#include <m/utility/byte_span.h>

#include <m/memory/aligned_allocation.h>

namespace m
{
    //
    // The allocator functions return byte_span objects which have a .data()
    // which is non-nullptr and a .size() component which is >= to the
    // size passed in.
    //

    using raw_aligned_allocator_fn_t = byte_span (*)(std::align_val_t alignment, std::size_t bytes);

    //
    // The raw_minaligned_allocator_fn_t return memory which is at least
    // aligned to __STDCPP_DEFAULT_NEW_ALIGNMENT__.
    //
    // It is common to refer to this class of allocators as "unaligned" but
    // the common practice in C++ is that they are actually required to
    // return memory allocated to an implementation defined minimum
    // value (__STDCPP_DEFAULT_NEW_ALIGNMENT__).
    //
    // There is utility in allocators that are truly unaligned, and so here
    // we use the term minaligned to denote that these allocators are not
    // actually unaligned so much as only guaranteeing the minimal alignment.
    //

    using raw_minaligned_allocator_fn_t = byte_span (*)(std::size_t bytes);

    //
    // The deallocation functions match the allocation functions.
    //
    // The byte_span passed in must have the same .data() component that was
    // returned by the corresponding allocator.
    //
    // The .size() component may range between the `bytes` that was passed
    // in to the allocator to the `.size()` that was returned by the allocator.
    // (in practice, it should be one or the other.)
    //
    // For fixed sized allocations, the simple pattern is to pass the size of
    // the type allocated.
    //
    // For variable sized allocations, either the size of the allocation will
    // be stored with the allocation, or the means to recompute the requested
    // bytes, so pass one of those.
    //
    using raw_aligned_deallocator_fn_t    = void (*)(byte_span);
    using raw_minaligned_deallocator_fn_t = void (*)(byte_span);

    //
    // A raw_allocation_traits type has the following characteristics:
    //
    // Member Types:
    //
    //  value_type - The value type managed
    //
    // Member Functions:
    //
    //  allocate [static] - allocates raw bytes. Since the traits knows the
    //                      value type, only the byte count is passed, the
    //                      alignment, if required, is alignof(value_type).
    //
    //  deallocate [static] - deallocates raw bytes.
    //
    //  default_construct_n [static] - see the equivalent
    //                      std::uninitialized_default_construct_n
    //                      definition for semantics.
    //
    //  destroy_n [static] - see the equivalent std::destroy_n definition
    //                      for semantics.
    //
    // Data Members:
    //
    //
    //

    template <typename T>
    struct basic_base_raw_allocation_helper_traits
    {
        using size_type  = std::size_t;
        using value_type = T;

        constexpr static void
        uninitialized_default_construct_n(std::span<T> s) noexcept
        {
            std::uninitialized_default_construct_n(s.data(), s.size());
        }

        template <typename U>
        constexpr static void
        uninitialized_fill_n(std::span<T> s, U const& u) noexcept
        {
            std::uninitialized_fill_n(s.data(), s.size(), u);
        }

        constexpr static void
        default_construct_remainder(byte_span) noexcept
        {
            // do nothing by default
        }

        constexpr static void
        destroy_n(std::span<T> s)
        {
            std::destroy_n(s.data(), s.size());
        }

        constexpr static void
        destroy_remainder(byte_span) noexcept
        {
            // do nothing by default
        }
    };

    template <typename T>
    struct basic_aligned_raw_allocation_helper_traits : basic_base_raw_allocation_helper_traits<T>
    {
        static byte_span
        allocate(std::size_t bytes)
        {
            return aligned_alloc(static_cast<std::align_val_t>(alignof(T)), bytes);
        }

        static void
        deallocate(byte_span span)
        {
            aligned_free(span);
        }
    };

    template <typename T>
    struct basic_minaligned_raw_allocation_helper_traits :
        basic_base_raw_allocation_helper_traits<T>
    {
        static byte_span
        allocate(std::size_t bytes)
        {
            auto const ptr = ::new std::byte[bytes];
            M_INTERNAL_ERROR_CHECK(ptr != nullptr);

            return byte_span(ptr, bytes);
        }

        static void
        deallocate(byte_span span)
        {
            delete[] span.data();
        }
    };

    template <typename T>
    using basic_raw_allocation_helper_traits =
        std::conditional_t<requires_aligned_allocator_t<T>,
                           basic_aligned_raw_allocation_helper_traits<T>,
                           basic_minaligned_raw_allocation_helper_traits<T>>;

    template <typename T>
    struct raw_allocation_result
    {
        std::span<T> m_value_span;
        byte_span    m_remainder_span;
    };

    template <typename RawAllocationTraitsT>
    class raw_allocation_helper
    {
    public:
        using traits_type = RawAllocationTraitsT;

        using value_type = typename traits_type::value_type;

        using raw_allocation_result_type = raw_allocation_result<value_type>;
        using size_type                  = typename traits_type::size_type;
        using value_span_type            = std::span<value_type>;

        struct parameters_t
        {
            size_type n;
            size_type additional_bytes;
        };

        raw_allocation_helper(parameters_t parameters, const value_type& default_value):
            m_n(parameters.n), m_additional_bytes(parameters.additional_bytes)
        {
            allocate();
            fill(default_value);
        }

        raw_allocation_helper(parameters_t parameters):
            m_n(parameters.n), m_additional_bytes(parameters.additional_bytes)
        {
            allocate();
            default_construct();
        }

        raw_allocation_result_type
        get() const
        {
            return raw_allocation_result_type{.m_value_span     = this->m_value_span,
                                              .m_remainder_span = this->m_remainder_span};
        }

        raw_allocation_result_type
        release()
        {
            auto const value_span     = std::exchange(m_value_span, value_span_type{});
            auto const remainder_span = std::exchange(m_remainder_span, byte_span{});
            auto const raw_byte_span  = std::exchange(m_byte_span, byte_span{});
            std::ignore               = raw_byte_span;

            return raw_allocation_result_type{.m_value_span     = value_span,
                                              .m_remainder_span = remainder_span};
        }

        void
        reset()
        {
            auto const value_span     = std::exchange(m_value_span, value_span_type{});
            auto const remainder_span = std::exchange(m_remainder_span, byte_span{});
            auto const raw_byte_span  = std::exchange(m_byte_span, byte_span{});
            std::ignore               = raw_byte_span;

            traits_type::destroy_n(value_span);

            if (remainder_span.size() != 0)
                traits_type::destroy_remainder(remainder_span);
        }

    private:
        void
        allocate()
        {
            auto const t_only_bytes = m::math::multiply(m_n, sizeof(value_type), size_type{});
            auto const bytes        = m::math::add(t_only_bytes, m_additional_bytes, size_type{});
            m_byte_span             = traits_type::allocate(bytes);
            // Contractually, the traits type must have returned a span of
            // sufficient size, but let us verify.
            M_INTERNAL_ERROR_CHECK(m_byte_span.size() >= bytes);
            m_value_span = as_writable_ts<value_type>(m_byte_span, m_n, m_remainder_span);
            M_INTERNAL_ERROR_CHECK(m_value_span.size() == m_n);
        }

        template <typename U>
        void
        fill(U const& value)
        {
            traits_type::uninitialized_fill_n(m_value_span, value);
        }

        void
        default_construct()
        {
            traits_type::uninitialized_default_construct_n(m_value_span);
        }

        size_type       m_n;
        size_type       m_additional_bytes;
        byte_span       m_byte_span;
        value_span_type m_value_span;
        byte_span       m_remainder_span;
    };

} // namespace m

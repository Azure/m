// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <limits>
#include <malloc.h>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <random>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>

#include <m/byte_streams/byte_streams.h>
#include <m/error_handling/macros.h>
#include <m/math/math.h>

namespace m
{
    std::span<std::byte>
    aligned_alloc(std::align_val_t alignment, std::size_t bytes);

    void
    aligned_free(std::span<std::byte> s);

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

        raw_array_allocator(): m_span{}, m_constructed{} {}

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
                                               size_type constructed = size_type{}) noexcept:
            m_span(ptr, size), m_constructed(constructed)
        {}

        raw_array_allocator(std::size_t const count): m_span{}, m_constructed{}
        {
            if (count > max_count)
                throw std::bad_array_new_length();

            auto const bytes = count * sizeof(T);

            auto const allocated_span = aligned_alloc(std::align_val_t{alignof(T)}, bytes);
            auto const data_as_t      = reinterpret_cast<value_type*>(allocated_span.data());
            m_span                    = span_type(data_as_t, count);
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
                if (auto const constructed = std::exchange(m_constructed, size_type{});
                    constructed != size_type{})
                {
                    std::destroy_n(s.data(), constructed);
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

        std::span<T>
        release()
        {
            if ((m_constructed != 0) && (m_constructed != m_span.size()))
                throw std::runtime_error("Cannot release after failed default construction");

            auto const s  = std::exchange(m_span, span_type{});
            m_constructed = 0;
            return s;
        }

        void
        operator=(raw_array_allocator const&) = delete;

        constexpr size_type
        size() const
        {
            return m_span.size();
        }

        constexpr size_type
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
            if (m_constructed != 0)
                throw std::runtime_error("Already constructed");

            auto const begin_it = begin();
            auto       it       = begin_it;
            auto const end_it   = end();

            while (it != end_it)
            {
                ::new (it++) value_type();
                m_constructed++;
            }
        }

    private:
        std::span<value_type> m_span;
        size_type             m_constructed;
    };

    template <typename T>
    class unique_span
    {
    public:
        using element_type     = T;
        using value_type       = std::decay_t<T>;
        using const_value_type = std::add_const_t<value_type>;
        using size_type        = std::size_t;
        using difference_type  = std::ptrdiff_t;
        using pointer          = T*;
        using const_pointer    = T const*;
        using reference        = T&;
        using const_reference  = T const&;
        using span_type        = std::span<T, std::dynamic_extent>;
        using iterator         = typename span_type::iterator;
        using reverse_iterator = typename span_type::reverse_iterator;

#ifdef M_HAS_CXX23
        using const_iterator         = typename span_type::const_iterator;
        using const_reverse_iterator = typename span_type::const_reverse_iterator;
#endif

        constexpr unique_span() noexcept: m_span() {}

        constexpr unique_span(unique_span&& other): m_span()
        {
            using std::swap;

            swap(m_span, other.m_span);
        }

        unique_span(std::size_t n): m_span()
        {
            m::raw_array_allocator<value_type> ra(n);
            ra.default_construct();
            m_span = ra.release_span();
        }

        template <typename Fn>
        unique_span(std::size_t n, Fn&& fn): m_span()
        {
            m::raw_array_allocator<value_type> ra(n);

            for (std::size_t i = 0; i < n; i++)
                std::invoke(fn, i, ra[i]);

            m_span = ra.release();
        }

        /// <summary>
        /// Constructs the unique_span based on the std::initializer_list
        /// passed in.
        ///
        /// NOTE: this implementation currently default-constructs the data
        /// array, and then copies the data on top of it.
        /// </summary>
        /// <param name="il"></param>
        unique_span(std::initializer_list<T> il): m_span()
        {
            m::raw_array_allocator<value_type> ra(il.size());

            std::uninitialized_copy_n(il.begin(), il.size(), ra.get());

            m_span = ra.release_span();
        }

        /// <summary>
        /// Constructor that permits initialization from spans with static
        /// extent as well as dynamic
        ///
        /// NOTE: this implementation currently default-constructs the data
        /// array, and then copies the data on top of it.
        /// </summary>
        /// <typeparam name="N">Not used except to match the span `s`'s type.</typeparam>
        /// <param name="s"></param>
        template <std::size_t N>
        unique_span(std::span<T, N> s): m_span()
        {
            m::raw_array_allocator<value_type> ra(s.size());

            std::uninitialized_copy_n(s.begin(), s.size(), ra.get());

            m_span = ra.release();
        }

        template <typename IteratorT, typename EndIteratorT>
            requires(std::random_access_iterator<IteratorT>)
        unique_span(IteratorT it, EndIteratorT end)
        {
            constexpr auto                     size = std::distance(it, end);
            m::raw_array_allocator<value_type> ra(size);
            std::uninitialized_copy(it, end, ra.get());
            m_span = ra.release();
        }

        template <typename RangeT>
        unique_span(RangeT&& r)
        {
            auto size = static_cast<std::size_t>(std::ranges::size(r));
            m::raw_array_allocator<value_type> ra(size);
            std::ranges::uninitialized_copy(r, ra);
            m_span = ra.release();
        }

        ~unique_span() { reset(); }

        void
        reset()
        {
            auto const s = std::exchange(m_span, std::span<value_type, std::dynamic_extent>());

            if (auto const ptr = s.data(); ptr != nullptr)
            {
                std::ranges::destroy(s);

                // Have raw_array_allocator take control over the allocation again
                // so that it can deallocate it, however it had allocated
                // it.
                raw_array_allocator ra(ptr, s.size());
            }
        }

        constexpr
        operator std::span<T>() const noexcept
        {
            return m_span;
        }

        constexpr std::span<T>
        span() const noexcept
        {
            return m_span;
        }

        /// <summary>
        /// Introduce implicit conversion to std::span<T const> if
        /// T was not const.
        /// </summary>
        constexpr
        operator std::span<const_value_type>() const noexcept
            requires(!std::is_same_v<element_type, const_value_type>)
        {
            return std::span<const_value_type, std::dynamic_extent>(
                const_cast<const_value_type*>(m_span.data()), m_span.size());
        }

        constexpr T*
        data() const noexcept
        {
            return m_span.data();
        }

        constexpr std::size_t
        size() const noexcept
        {
            return m_span.size();
        }

        auto
        cbegin() const
        {
            return m_span.cbegin();
        }

        auto
        cend() const
        {
            return m_span.cend();
        }

        auto
        begin() const
        {
            return m_span.cbegin();
        }

        auto
        end() const
        {
            return m_span.cend();
        }

    private:
        std::span<value_type> m_span;
    };

    template <typename T>
    unique_span(T&&) -> unique_span<std::remove_reference_t<std::ranges::range_reference_t<T>>>;

    //
    // Really, the notion here of "Random access stream and a position"
    // should be somehow abstracted into a "loading context", and then
    // this same code would apply to "a sequential stream at its current
    // position" equally well to a random access stream at a given
    // position.
    //
    // I don't know how to do that today without introducing some kind
    // of formal interface which would introduce serious inefficiency
    // both in terms of levels of indirection at runtime as well as
    // coding complexity. TBD. It would be better not to have notions
    // of random access byte streams at this point in the namespace,
    // but otherwise this general "memory" concept will be buried in
    // the byte_stream namespace.
    //
    // TODO: Require that T be trivially copyable, using a Concept. This
    // can be done easily today using SNIFAE but I wanted to hold off
    // for use of better practices. The intent of these classes are
    // decoding PE32 files and the types are, in general, DWORDs,
    // WORDs, and character strings so this is not intended for general
    // purpose serialization / deserialization support as of yet.
    //
    template <typename T, typename SourceT>
    T
    load_from(SourceT s, io::position_t p)
    {
        T v{};
        if (s->read(p, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
            throw std::runtime_error("end of file");
        return v;
    }

    template <typename T, typename SourceT>
    void
    load_into(T& v, SourceT s, io::position_t p)
    {
        if (s->read(p, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
            throw std::runtime_error("end of file");
    }

    template <typename T, typename SourceT>
    void
    load_into(T& v, SourceT s, io::position_t origin, std::size_t /* limit */)
    {
        if (s->read(origin, std::as_writable_bytes(std::span(&v, 1))) != sizeof(T))
            throw std::runtime_error("end of file");
    }

    template <typename SourceT>
    class load_from_position_context
    {
    public:
        using offset_t   = io::offset_t;
        using position_t = io::position_t;

        load_from_position_context(SourceT     s,
                                   position_t  origin,
                                   std::size_t limit = (std::numeric_limits<std::size_t>::max)()):
            m_s(s), m_origin(origin), m_limit(limit)
        {}

        template <typename T>
        void
        load_into(T& v, offset_t offset) const
        {

            m::load_into(v, m_s, m_origin + offset, m_limit);
        }

    private:
        SourceT     m_s;
        position_t  m_origin;
        std::size_t m_limit;
    };

    template <typename S>
    load_from_position_context(S, m::io::position_t, std::size_t) -> load_from_position_context<S>;

    template <typename S>
    load_from_position_context(S, m::io::position_t) -> load_from_position_context<S>;

    template <typename SourceT, typename TargetT>
    using data_member_loader_t = void (*)(TargetT&, load_from_position_context<SourceT> const&);

    template <typename SourceT, typename TargetT>
    void
    load_data_members(load_from_position_context<SourceT> const&        lfpc,
                      TargetT&                                          target,
                      std::span<data_member_loader_t<SourceT, TargetT>> span)
    {
        for (auto&& f: span)
        {
            f(target, lfpc);
        }
    }

} // namespace m

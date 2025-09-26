// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <source_location>
#include <stdexcept>
#include <stdio.h>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/utility/pointers.h>
#include <m/utility/smallest_size.h>

namespace m
{
    /*
     * SPDX-FileCopyrightText: Copyright (c) 2023 Gonzalo Brito Gadeschi. All rights reserved.
     * SPDX-License-Identifier: MIT
     *
     * Permission is hereby granted, free of charge, to any person obtaining a
     * copy of this software and associated documentation files (the "Software"),
     * to deal in the Software without restriction, including without limitation
     * the rights to use, copy, modify, merge, publish, distribute, sublicense,
     * and/or sell copies of the Software, and to permit persons to whom the
     * Software is furnished to do so, subject to the following conditions:
     *
     * The above copyright notice and this permission notice shall be included in
     * all copies or substantial portions of the Software.
     *
     * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
     * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
     * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
     * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
     * LIABILITY, WHETHER IN AN ACTION OF Precondition, TORT OR OTHERWISE, ARISING
     * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
     * DEALINGS IN THE SOFTWARE.
     */

#pragma push_macro("M_IV_ASSUME")
#pragma push_macro("M_IV_ASSERT")
#pragma push_macro("M_IV_EXPECT")

// Optimizer allowed to assume that EXPR evaluates to true
#define M_IV_ASSUME(X) static_cast<void>((X) ? void(0) : std::unreachable())

// Assert pretty printer
#define M_IV_ASSERT(...)                                                                           \
    static_cast<void>((__VA_ARGS__) ?                                                              \
                          void(0) :                                                                \
                          ::m::inplace_vector_impl::assert_failure(                                \
                              std::source_location::current(), "assertion failed: " #__VA_ARGS__))

// Assert in debug, assume in release.
#ifdef NDEBUG
#define M_IV_EXPECT(X) M_IV_ASSUME(X)
#else
#define M_IV_EXPECT(X) M_IV_ASSERT(X)
#endif

    // Private utilitIes
    namespace inplace_vector_impl
    {
        template <typename = void>
        [[noreturn]]
        static constexpr void
        assert_failure(std::source_location const& sl, char const* msg)
        {
            throw m::assertion_failure(sl, msg);
        }

        // Index a random-access and sized range doing bound checks in debug builds
        template <std::ranges::random_access_range RangeT, std::integral IndexT>
        static constexpr decltype(auto)
        index(RangeT&& rng, IndexT idx) noexcept
            requires(std::ranges::sized_range<RangeT>)
        {
            M_IV_EXPECT(static_cast<ptrdiff_t>(idx) < std::ranges::size(rng));
            return std::begin(std::forward<RangeT>(rng))[std::forward<IndexT>(idx)];
        }

        // http://eel.is/c++draft/container.requirements.general#container.intro.reqmts-2
        template <typename RangeT, typename T>
        concept container_compatible_range =
            std::ranges::input_range<RangeT> &&
            std::convertible_to<std::ranges::range_reference_t<RangeT>, T>;

        template <typename PointerT, typename T>
        concept move_or_copy_insertable_from = requires(PointerT ptr, T&& value) {
            { std::construct_at(ptr, std::forward<T&&>(value)) } -> std::same_as<PointerT>;
        };

        // Types implementing the `inplace_vector`'s storage
        namespace storage
        {
            // TODO: flesh out
            template <typename T, std::size_t N>
            struct aligned_storage2
            {
                alignas(T) std::array<std::byte, sizeof(T) * N> m_data;

                constexpr T*
                data(size_t idx) noexcept
                {
                    M_IV_EXPECT(idx < N);
                    return reinterpret_cast<T*>(m_data.data()) + idx;
                }

                constexpr const T*
                data(size_t idx) const noexcept
                {
                    M_IV_EXPECT(idx < N);
                    return reinterpret_cast<const T*>(m_data.data()) + idx;
                }
            };

            // Storage for zero elements.
            template <typename T>
            struct zero_sized
            {
            protected:
                using size_type = uint8_t;

                static constexpr T*
                data() noexcept
                {
                    return nullptr;
                }

                static constexpr size_type
                size() noexcept
                {
                    return 0;
                }

                static constexpr void
                unsafe_set_size(size_t new_size) noexcept
                {
                    M_IV_EXPECT(new_size == 0 &&
                                "tried to change size of empty storage to non-zero value");
                }

            public:
                constexpr zero_sized()                  = default;
                constexpr zero_sized(zero_sized const&) = default;
                constexpr zero_sized&
                operator=(zero_sized const&)       = default;
                constexpr zero_sized(zero_sized&&) = default;
                constexpr zero_sized&
                operator=(zero_sized&&) = default;
                constexpr ~zero_sized() = default;
            };

            // Storage for trivial types.
            template <typename T, std::size_t N>
            struct trivial
            {
                // This static_assert() cannot be a requires() clause because
                // std::conditional_t<> evidently simultaneously evaluates both
                // branches so the fact that one branch or the other is or is
                // not trivial breaks the use of std::conditional_t<> to
                // choose between this type and `non_trivial`.
                static_assert(std::is_trivial_v<T> && (N != 0));

            protected:
                using size_type = smallest_size_t<N>;

            private:
                // If value_type is const, then const array of non-const elements:
                using data_t = std::conditional_t<!std::is_const_v<T>,
                                                  std::array<T, N>,
                                                  const std::array<std::remove_const_t<T>, N>>;

                alignas(T) data_t m_data{};
                size_type m_size{0};

            protected:
                constexpr const T*
                data() const noexcept
                {
                    return m_data.data();
                }

                constexpr T*
                data() noexcept
                {
                    return m_data.data();
                }

                constexpr size_type
                size() const noexcept
                {
                    return m_size;
                }

                constexpr void
                unsafe_set_size(std::size_t new_size) noexcept
                {
                    M_IV_EXPECT(new_size <= N && "new_size out-of-bounds [0, N]");
                    m_size = static_cast<size_type>(new_size);
                }

            public:
                constexpr trivial() noexcept               = default;
                constexpr trivial(trivial const&) noexcept = default;
                constexpr trivial&
                operator=(trivial const&) noexcept    = default;
                constexpr trivial(trivial&&) noexcept = default;
                constexpr trivial&
                operator=(trivial&&) noexcept = default;
                constexpr ~trivial()          = default;
            };

            /// Storage for non-trivial elements.
            template <typename T, std::size_t N>
            struct non_trivial
            {
                // This static_assert() cannot be a requires() clause because
                // std::conditional_t<> evidently simultaneously evaluates both
                // branches so the fact that one branch or the other is or is
                // not trivial breaks the use of std::conditional_t<> to
                // choose between this type and `trivial`.
                static_assert(!std::is_trivial_v<T> && (N != 0));

            protected:
                using size_type = smallest_size_t<N>;

            private:
                using data_t =
                    std::conditional_t<!std::is_const_v<T>,
                                       aligned_storage2<T, N>,
                                       const aligned_storage2<std::remove_const_t<T>, N>>;
                data_t    m_data{}; // BUGBUG: test SIMD types
                size_type m_size = 0;

            protected:
                constexpr const T*
                data() const noexcept
                {
                    return m_data.data(0);
                }
                constexpr T*
                data() noexcept
                {
                    return m_data.data(0);
                }
                constexpr size_type
                size() const noexcept
                {
                    return m_size;
                }
                constexpr void
                unsafe_set_size(std::size_t new_size) noexcept
                {
                    M_IV_EXPECT(new_size <= N && "new_size out-of-bounds [0, N)");
                    m_size = static_cast<size_type>(new_size);
                }

            public:
                constexpr non_trivial() noexcept                   = default;
                constexpr non_trivial(non_trivial const&) noexcept = default;
                constexpr non_trivial&
                operator=(non_trivial const&) noexcept        = default;
                constexpr non_trivial(non_trivial&&) noexcept = default;
                constexpr non_trivial&
                operator=(non_trivial&&) noexcept = default;
                constexpr ~non_trivial()          = default;
            };

            template <typename T, std::size_t N>
            using select_trivial_or_nontrivial_t =
                std::conditional_t<std::is_trivial_v<T>, trivial<T, N>, non_trivial<T, N>>;

            // Selects the vector storage.
            template <typename T, std::size_t N>
            using storage_t =
                std::conditional_t<N == 0, zero_sized<T>, select_trivial_or_nontrivial_t<T, N>>;

        } // namespace storage
    } // namespace inplace_vector_impl

    /// Dynamically-resizable fixed-N vector with inplace storage.
    template <typename T, std::size_t N>
        requires(std::is_nothrow_destructible_v<T>)
    struct inplace_vector : private inplace_vector_impl::storage::storage_t<T, N>
    {
    private:
        using base_type = inplace_vector_impl::storage::storage_t<T, N>;
        using self_type = inplace_vector<T, N>;
        using base_type::data;
        using base_type::size;
        using base_type::unsafe_set_size;

    public:
        using value_type             = T;
        using pointer                = T*;
        using const_pointer          = const T*;
        using reference              = value_type&;
        using const_reference        = const value_type&;
        using size_type              = size_t;
        using difference_type        = ptrdiff_t;
        using iterator               = pointer;
        using const_iterator         = const_pointer;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        // [containers.sequences.inplace_vector.cons], construct/copy/destroy
        constexpr inplace_vector() noexcept { unsafe_set_size(0); }
        // constexpr explicit inplace_vector(size_type n);
        // constexpr inplace_vector(size_type n, const T& value);
        // template <typename InputIt>  // BUGBUG: why not model input_iterator?
        //   constexpr inplace_vector(InputIt first, InputIt last);
        // template <inplace_vector_impl::container_compatible_range<T> RangeT>
        //  constexpr inplace_vector(from_range_t, RangeT&& rnge);
        // from base-class, trivial if is_trivially_copy_constructible_v<T>:
        //   constexpr inplace_vector(const inplace_vector&);
        // from base-class, trivial if is_trivially_move_constructible_v<T>
        //   constexpr inplace_vector(inplace_vector&&) noexcept(N == 0 ||
        //   is_nothrow_move_constructible_v<T>);
        // constexpr inplace_vector(initializer_list<T> il);
        // from base-class, trivial if is_trivially_destructible_v<T>
        //   constexpr ~inplace_vector();
        // from base-class, trivial if is_trivially_destructible_v<T> &&
        // is_trivially_copy_assignable_v<T>
        //   constexpr inplace_vector& operator=(const inplace_vector& __other);
        // from base-class, trivial if is_trivially_destructible_v<T> &&
        // is_trivially_copy_assignable_v<T>
        //   constexpr inplace_vector& operator=(inplace_vector&& __other) noexcept(N == 0 ||
        //   is_nothrow_move_assignable_v<T>);
        // template <typename InputIt> // BUGBUG: why not model input_iterator
        //  constexpr void assign(InputIt first, InputIt l__ast);
        // template<inplace_vector_impl::container_compatible_range<T> RangeT>
        //  constexpr void assign_range(RangeT&& rnge);
        // constexpr void assign(size_type n, const T& u);
        // constexpr void assign(initializer_list<T> il);

        // iterators
        constexpr iterator
        begin() noexcept
        {
            return data();
        }
        constexpr const_iterator
        begin() const noexcept
        {
            return data();
        }
        constexpr iterator
        end() noexcept
        {
            return begin() + size();
        }
        constexpr const_iterator
        end() const noexcept
        {
            return begin() + size();
        }
        constexpr reverse_iterator
        rbegin() noexcept
        {
            return reverse_iterator(end());
        }
        constexpr const_reverse_iterator
        rbegin() const noexcept
        {
            return const_reverse_iterator(end());
        }
        constexpr reverse_iterator
        rend() noexcept
        {
            return reverse_iterator(begin());
        }
        constexpr const_reverse_iterator
        rend() const noexcept
        {
            return const_reverse_iterator(begin());
        }

        constexpr const_iterator
        cbegin() const noexcept
        {
            return data();
        }
        constexpr const_iterator
        cend() const noexcept
        {
            return cbegin() + size();
        }
        constexpr const_reverse_iterator
        crbegin() const noexcept
        {
            return const_reverse_iterator(cend());
        }
        constexpr const_reverse_iterator
        crend() const noexcept
        {
            return const_reverse_iterator(cbegin());
        }

        [[nodiscard]] constexpr bool
        empty() const noexcept
        {
            return size() == 0;
        };

        constexpr size_type
        size() const noexcept
        {
            return base_type::size();
        }

        static constexpr size_type
        max_size() noexcept
        {
            return N;
        }

        static constexpr size_type
        capacity() noexcept
        {
            return N;
        }

        // constexpr void resize(size_type sz);
        // constexpr void resize(size_type sz, const T& c);

        constexpr void
        reserve(size_type n)
        {
            if (n > N) [[unlikely]]
                throw std::bad_alloc();
        }
        constexpr void
        shrink_to_fit()
        {}

        // element access
        constexpr reference
        operator[](size_type n)
        {
            return inplace_vector_impl::index(*this, n);
        }
        constexpr const_reference
        operator[](size_type n) const
        {
            return inplace_vector_impl::index(*this, n);
        }
        // constexpr const_reference at(size_type n) const;
        // constexpr reference       at(size_type n);
        constexpr reference
        front()
        {
            return inplace_vector_impl::index(*this, size_type(0));
        }
        constexpr const_reference
        front() const
        {
            return inplace_vector_impl::index(*this, size_type(0));
        }
        constexpr reference
        back()
        {
            return inplace_vector_impl::index(*this, size() - size_type{1});
        }
        constexpr const_reference
        back() const
        {
            return inplace_vector_impl::index(*this, size() - size_type{1});
        }

        // [containers.sequences.inplace_vector.data], data access
        constexpr T*
        data() noexcept
        {
            return base_type::data();
        }

        constexpr const T*
        data() const noexcept
        {
            return base_type::data();
        }

        // [containers.sequences.inplace_vector.modifiers], modifiers
        // template <typename... Args>
        //  constexpr T& emplace_back(Args&&... args);
        // constexpr T& push_back(const T& x);
        // constexpr T& push_back(T&& x);
        // template<inplace_vector_impl::container_compatible_range<T> RangeT>
        //  constexpr void append_range(RangeT&& rnge);
        // constexpr void pop_back();

        // template<typename... Args>
        //  constexpr T* try_emplace_back(Args&&... args);
        // constexpr T* try_push_back(const T& value);
        // constexpr T* try_push_back(T&& value);

        // template<typename... Args>
        //  constexpr T& unchecked_emplace_back(Args&&... args);
        // constexpr T& unchecked_push_back(const T& value);
        // constexpr T& unchecked_push_back(T&& value);

        // template <typename... Args>
        //  constexpr iterator emplace(const_iterator pos, Args&&... args);
        // constexpr iterator insert(const_iterator pos, const T& x);
        // constexpr iterator insert(const_iterator pos, T&& x);
        // constexpr iterator insert(const_iterator pos, size_type n, const T& x);
        // template <typename InputIt>
        //  constexpr iterator insert(const_iterator pos, InputIt first,
        //  InputIt last);
        // template<inplace_vector_impl::container_compatible_range<T> RangeT>
        //   constexpr iterator insert_range(const_iterator pos, RangeT&& rnge);
        // constexpr iterator insert(const_iterator pos, initializer_list<T> il);
        // constexpr iterator erase(const_iterator pos);
        // constexpr iterator erase(const_iterator first, const_iterator last);
        // constexpr void swap(inplace_vector& x)
        //  noexcept(N == 0 || (is_nothrow_swappable_v<T> &&
        //  is_nothrow_move_constructible_v<T>));
        // constexpr void clear() noexcept;

        constexpr friend bool
        operator==(const inplace_vector& x, const inplace_vector& y)
        {
            return x.size() == y.size() && ::std::ranges::equal(x, y);
        }
        // constexpr friend auto /*synth-three-way-result<T>*/
        //  operator<=>(const inplace_vector& x, const inplace_vector& y);
        constexpr friend void
        swap(inplace_vector& x,
             inplace_vector& y) noexcept(N == 0 || (std::is_nothrow_swappable_v<T> &&
                                                    std::is_nothrow_move_constructible_v<T>))
        {
            x.swap(y);
        }

    private: // Utilities
        constexpr void
        internal_assert_iterator_in_range(const_iterator it) noexcept
        {
            M_IV_EXPECT(begin() <= it && "iterator not in range");
            M_IV_EXPECT(it <= end() && "iterator not in range");
        }

        constexpr void
        internal_assert_valid_iterator_pair(const_iterator first, const_iterator last) noexcept
        {
            M_IV_EXPECT(first <= last && "invalid iterator pair");
        }

        constexpr void
        internal_assert_iterator_pair_in_range(const_iterator first, const_iterator last) noexcept
        {
            internal_assert_iterator_in_range(first);
            internal_assert_iterator_in_range(last);
            internal_assert_valid_iterator_pair(first, last);
        }

        constexpr void
        internal_unsafe_destroy(T* first, T* last) noexcept(std::is_nothrow_destructible_v<T>)
        {
            internal_assert_iterator_pair_in_range(first, last);
            if constexpr (N > 0 && !std::is_trivial_v<T>)
            {
                for (; first != last; ++first)
                    first->~T();
            }
        }

    public:
        // Implementation

        // [containers.sequences.inplace_vector.modifiers], modifiers

        template <typename... Args>
            requires(std::constructible_from<T, Args...>)
        constexpr T&
        unchecked_emplace_back(Args&&... args)
        {
            M_IV_EXPECT(size() < capacity() && "inplace_vector out-of-memory");
            std::construct_at(end(), std::forward<Args>(args)...);
            unsafe_set_size(size() + size_type{1});
            return back();
        }

        template <typename... Args>
            requires(std::constructible_from<T, Args...>)
        constexpr T*
        try_emplace_back(Args&&... args)
        {
            if (size() == capacity()) [[unlikely]]
                return nullptr;
            return &unchecked_emplace_back(std::forward<Args>(args)...);
        }

        template <typename... Args>
            requires(std::constructible_from<T, Args...>)
        constexpr void
        emplace_back(Args&&... args)
        {
            if (!try_emplace_back(std::forward<Args>(args)...)) [[unlikely]]
                throw std::bad_alloc();
        }
        constexpr T&
        push_back(const T& x)
            requires(std::constructible_from<T, T const&>)
        {
            emplace_back(x);
            return back();
        }
        constexpr T&
        push_back(T&& x)
            requires(std::constructible_from<T, T &&>)
        {
            emplace_back(std::forward<T&&>(x));
            return back();
        }

        constexpr T*
        try_push_back(const T& x)
            requires(std::constructible_from<T, T const&>)
        {
            return try_emplace_back(x);
        }

        constexpr T*
        try_push_back(T&& x)
            requires(std::constructible_from<T, T &&>)
        {
            return try_emplace_back(std::forward<T&&>(x));
        }

        constexpr T&
        unchecked_push_back(T const& x)
            requires(std::constructible_from<T, T const&>)
        {
            return unchecked_emplace_back(x);
        }

        constexpr T&
        unchecked_push_back(T&& x)
            requires(std::constructible_from<T, T &&>)
        {
            return unchecked_emplace_back(std::forward<T&&>(x));
        }

        template <inplace_vector_impl::container_compatible_range<T> RangeT>
            requires(std::constructible_from<T, std::ranges::range_reference_t<RangeT>>)
        constexpr void
        append_range(RangeT&& rnge)
        {
            if constexpr (std::ranges::sized_range<RangeT>)
            {
                if (size() + std::ranges::size(rnge) > capacity()) [[unlikely]]
                    throw std::bad_alloc();
            }
            for (auto&& e: rnge)
            {
                if (size() == capacity()) [[unlikely]]
                    throw std::bad_alloc();
                emplace_back(std::forward<decltype(e)>(e));
            }
        }

        template <typename... Args>
            requires(std::constructible_from<T, Args...> && std::movable<T>)
        constexpr iterator
        emplace(const_iterator pos, Args&&... args)
        {
            internal_assert_iterator_in_range(pos);
            auto b = end();
            emplace_back(std::forward<Args>(args)...);
            auto newpos = begin() + (pos - begin());
            rotate(newpos, b, end());
            return newpos;
        }

        template <typename InputIt>
        constexpr iterator
        insert(const_iterator pos, InputIt first, InputIt last)
            requires(std::constructible_from<T, std::iter_reference_t<InputIt>> && std::movable<T>)
        {
            internal_assert_iterator_in_range(pos);
            // internal_assert_valid_iterator_pair(first, last);
            if constexpr (std::random_access_iterator<InputIt>)
            {
                if (size() + static_cast<size_type>(std::distance(first, last)) > capacity())
                    [[unlikely]]
                    throw std::bad_alloc{};
            }
            auto b = end();
            for (; first != last; ++first)
                emplace_back(std::move(*first));
            auto newpos = begin() + (pos - begin());
            std::rotate(newpos, b, end());
            return newpos;
        }

        template <inplace_vector_impl::container_compatible_range<T> RangeT>
        constexpr iterator
        insert_range(const_iterator pos, RangeT&& rnge)
            requires(std::constructible_from<T, std::ranges::range_reference_t<RangeT>> &&
                     std::movable<T>)
        {
            return insert(pos, std::begin(rnge), std::end(rnge));
        }

        constexpr iterator
        insert(const_iterator pos, std::initializer_list<T> il)
            requires(
                std::constructible_from<T,
                                        std::ranges::range_reference_t<std::initializer_list<T>>> &&
                std::movable<T>)
        {
            return insert_range(pos, il);
        }

        constexpr iterator
        insert(const_iterator pos, size_type n, T const& x)
            requires(std::constructible_from<T, T const&> && std::copyable<T>)
        {
            internal_assert_iterator_in_range(pos);
            auto b = end();
            for (size_type idx = 0; idx < n; ++idx)
                emplace_back(x);
            auto newpos = begin() + (pos - begin());
            std::rotate(newpos, b, end());
            return newpos;
        }

        constexpr iterator
        insert(const_iterator pos, const T& x)
            requires(std::constructible_from<T, T const&> && std::copyable<T>)
        {
            return insert(pos, 1, x);
        }

        constexpr iterator
        insert(const_iterator pos, T&& x)
            requires(std::constructible_from<T, T &&> && std::movable<T>)
        {
            return emplace(pos, std::move(x));
        }

        constexpr inplace_vector(std::initializer_list<T> il)
            requires(
                std::constructible_from<T,
                                        std::ranges::range_reference_t<std::initializer_list<T>>> &&
                std::movable<T>)
        {
            insert(begin(), il);
        }

        constexpr inplace_vector(size_type n, T const& value)
            requires(std::constructible_from<T, T const&> && std::copyable<T>)
        {
            insert(begin(), n, value);
        }

        constexpr explicit inplace_vector(size_type n)
            requires(std::constructible_from<T, T &&> && std::default_initializable<T>)
        {
            for (size_type idx = 0; idx < n; ++idx)
                emplace_back(T{});
        }

        template <typename InputIt> // BUGBUG: why not ranges::input_iterator?
        constexpr inplace_vector(InputIt first, InputIt last)
            requires(std::constructible_from<T, std::iter_reference_t<InputIt>> && std::movable<T>)
        {
            insert(begin(), first, last);
        }

        template <inplace_vector_impl::container_compatible_range<T> RangeT>
        constexpr inplace_vector(std::from_range_t, RangeT&& rnge)
            requires(std::constructible_from<T, std::ranges::range_reference_t<RangeT>> &&
                     std::movable<T>)
        {
            insert_range(begin(), std::forward<RangeT&&>(rnge));
        }

        constexpr iterator
        erase(const_iterator first, const_iterator last)
            requires(std::movable<T>)
        {
            internal_assert_iterator_pair_in_range(first, last);
            iterator new_first = begin() + (first - begin());
            if (first != last)
            {
                internal_unsafe_destroy(std::move(new_first + (last - first), end(), new_first), end());
                unsafe_set_size(size() - static_cast<size_type>(last - first));
            }
            return new_first;
        }

        constexpr iterator
        erase(const_iterator pos)
            requires(std::movable<T>)
        {
            return erase(pos, pos + 1);
        }

        constexpr void
        clear() noexcept
        {
            internal_unsafe_destroy(begin(), end());
            unsafe_set_size(0);
        }

        constexpr void
        resize(size_type sz, T const& c)
            requires(std::constructible_from<T, T const&> && std::copyable<T>)
        {
            if (sz == size())
                return;
            else if (sz > N) [[unlikely]]
                throw std::bad_alloc{};
            else if (sz > size())
                insert(end(), sz - size(), c);
            else
            {
                internal_unsafe_destroy(begin() + sz, end());
                unsafe_set_size(sz);
            }
        }
        constexpr void
        resize(size_type sz)
            requires(std::constructible_from<T, T &&> && std::default_initializable<T>)
        {
            if (sz == size())
                return;
            else if (sz > N) [[unlikely]]
                throw std::bad_alloc{};
            else if (sz > size())
                while (size() != sz)
                    emplace_back(T{});
            else
            {
                internal_unsafe_destroy(begin() + sz, end());
                unsafe_set_size(sz);
            }
        }

        constexpr reference
        at(size_type newpos)
        {
            if (newpos >= size()) [[unlikely]]
                throw std::out_of_range("inplace_vector::at");
            return inplace_vector_impl::index(*this, newpos);
        }

        constexpr const_reference
        at(size_type newpos) const
        {
            if (newpos >= size()) [[unlikely]]
                throw std::out_of_range("inplace_vector::at");
            return inplace_vector_impl::index(*this, newpos);
        }

        constexpr void
        pop_back()
        {
            M_IV_EXPECT(size() > 0 && "pop_back from empty inplace_vector!");
            internal_unsafe_destroy(end() - 1, end());
            unsafe_set_size(size() - 1);
        }

        constexpr inplace_vector(const inplace_vector& x)
            requires(std::copyable<T>)
        {
            for (auto&& e: x)
                emplace_back(e);
        }

        constexpr inplace_vector(inplace_vector&& x)
            requires(std::movable<T>)
        {
            for (auto&& e: x)
                emplace_back(::std::move(e));
        }

        constexpr inplace_vector&
        operator=(inplace_vector const& x)
            requires(std::copyable<T>)
        {
            clear();
            for (auto&& e: x)
                emplace_back(e);
            return *this;
        }

        constexpr inplace_vector&
        operator=(inplace_vector&& x)
            requires(std::movable<T>)
        {
            clear();
            for (auto&& e: x)
                emplace_back(std::move(e));
            return *this;
        }

        constexpr void
        swap(inplace_vector& x) noexcept(N == 0 || (std::is_nothrow_swappable_v<T> &&
                                                    std::is_nothrow_move_constructible_v<T>))
            requires(std::movable<T>)
        {
            auto tmp = std::move(x);
            x        = std::move(*this);
            (*this)  = std::move(tmp);
        }

        template <typename InputIt>
        constexpr void
        assign(InputIt first, InputIt last)
            requires(std::constructible_from<T, std::iter_reference_t<InputIt>> && std::movable<T>)
        {
            clear();
            insert(begin(), first, last);
        }

        template <inplace_vector_impl::container_compatible_range<T> RangeT>
        constexpr void
        assign_range(RangeT&& rnge)
            requires(std::constructible_from<T, std::ranges::range_reference_t<RangeT>> &&
                     std::movable<T>)
        {
            assign(std::begin(rnge), std::end(rnge));
        }

        constexpr void
        assign(size_type n, const T& u)
            requires(std::constructible_from<T, T const&> && std::movable<T>)
        {
            clear();
            insert(begin(), n, u);
        }

        constexpr void
        assign(std::initializer_list<T> il)
            requires(
                std::constructible_from<T,
                                        std::ranges::range_reference_t<std::initializer_list<T>>> &&
                std::movable<T>)
        {
            clear();
            insert_range(begin(), il);
        }

        constexpr friend int /*synth-three-way-result<T>*/
        operator<=>(inplace_vector const& x, inplace_vector const& y)
        {
            if (x.size() < y.size())
                return -1;
            if (x.size() > y.size())
                return +1;

            bool all_equal = true;
            bool all_less  = true;
            for (size_type idx = 0; idx < x.size(); ++idx)
            {
                if (x[idx] < y[idx])
                    all_equal = false;
                if (x[idx] == y[idx])
                    all_less = false;
            }

            if (all_equal)
                return 0;
            if (all_less)
                return -1;
            return 1;
        }
    };

#pragma pop_macro("M_IV_ASSUME")
#pragma pop_macro("M_IV_ASSERT")
#pragma pop_macro("M_IV_EXPECT")

} // namespace m

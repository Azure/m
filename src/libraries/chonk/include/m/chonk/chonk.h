// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <compare>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/error_handling/macros.h>
#include <m/utility/pointers.h>

namespace m
{
    namespace chonk_impl
    {
        template <typename T>
        constexpr std::size_t
        most_ts()
        {
            return (std::numeric_limits<std::size_t>::max)() / sizeof(T);
        }
    } // namespace chonk_impl

    /// <summary>
    /// A `chonk` is a lot like `std::array` except that the data does
    /// not initialize until on demand.
    ///
    /// So in that way, it's more like `std::vector`, except that it's
    /// statically sized and the data is inline in the object.
    ///
    /// The members which C++26 calls out as having "contract violations"
    /// induce internal error checks which terminate the process on
    /// violation. For example, out of bounds access with operator[].
    ///
    /// Because C++23 at least has no way to declare uninitialized
    /// storage and the only way to initialize storage is through
    /// pointers of the correct type, and the only way to take pointers
    /// of "raw storage type" (meaning `unsigned char*` or `std::byte*`)
    /// to the correct types is via reinterpret_cast<> which is
    /// incompatible with constexpr, we are left with the problem that
    /// at the very least, any member functions which _may_ initialize
    /// uninitialized storage cannot be constexpr.
    ///
    /// For the other member functions, they could rely on the code
    /// keeping track of the pointer to the first object as a special
    /// case and then the rest of the array's properly typed pointers
    /// can be trivially derived from that value.
    ///
    /// Or we have to drop the constexpr assumption.
    ///
    /// The first makes the code more complex and adds a pointer to the
    /// storage, but we already have a size, so it's somewhat unlikely to
    /// in practice have an actual effect. Especially if we do some
    /// smart work to use a type that's sized towards the value of N.
    ///
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <typeparam name="N"></typeparam>
    template <typename T, std::size_t N>
        requires(std::semiregular<T> && N <= chonk_impl::most_ts<T>())
    class chonk;

    namespace chonk_impl
    {
        template <typename T, std::size_t N>
            requires(std::semiregular<std::remove_const_t<T>> && N <= most_ts<T>())
        class iterator
        {
        public:
            using iterator_concept  = std::contiguous_iterator_tag;
            using iterator_category = std::random_access_iterator_tag;
            using value_type        = T;
            using difference_type   = std::ptrdiff_t;
            using pointer           = T*;
            using reference         = T&;

            constexpr iterator(iterator const& other) noexcept:
                m_chonk_ptr(other.m_chonk_ptr), m_index(other.m_index)
            {}

            constexpr iterator(iterator&& other) noexcept
            {
                using std::swap;
                swap(m_chonk_ptr, other.m_chonk_ptr);
                swap(m_index, other.m_index);
            }

            [[nodiscard]] constexpr reference
            operator*() const noexcept
            {
                return *ptr();
            }

            [[nodiscard]] constexpr pointer
            operator->() const noexcept
            {
                return ptr();
            }

            constexpr iterator&
            operator++() noexcept
            {
                ++m_index;
                return *this;
            }

            constexpr iterator
            operator++(int) noexcept
            {
                iterator temp = *this;
                ++m_index;
                return temp;
            }

            constexpr iterator&
            operator--() noexcept
            {
                --m_index;
                return *this;
            }

            constexpr iterator
            operator--(int) noexcept
            {
                iterator temp = *this;
                --m_index;
                return temp;
            }

            constexpr iterator&
            operator+=(const ptrdiff_t offset) noexcept
            {
                m_index += offset;
                return *this;
            }

            constexpr iterator&
            operator-=(const ptrdiff_t offset) noexcept
            {
                m_index -= offset;
                return *this;
            }

            [[nodiscard]] constexpr ptrdiff_t
            operator-(const iterator& r) const noexcept
            {
                return m_index - r.m_index;
            }

            [[nodiscard]] constexpr reference
            operator[](const ptrdiff_t offset) const noexcept
            {
                return ptr()[offset];
            }

            [[nodiscard]] constexpr bool
            operator==(const iterator& r) const noexcept
            {
                return m_index == r.m_index;
            }

            [[nodiscard]] constexpr std::strong_ordering
            operator<=>(const iterator& r) const noexcept
            {
                return m_index <=> r.m_index;
            }

            [[nodiscard]] constexpr iterator
            operator+(const ptrdiff_t offset) const noexcept
            {
                iterator temp = *this;
                temp += offset;
                return temp;
            }

            [[nodiscard]] constexpr iterator
            operator-(const ptrdiff_t offset) const noexcept
            {
                iterator temp = *this;
                temp -= offset;
                return temp;
            }

            [[nodiscard]] friend constexpr iterator
            operator+(const ptrdiff_t offset, iterator next) noexcept
            {
                next += offset;
                return next;
            }

        private:
            using chonk_type = chonk<std::remove_const_t<T>, N>;
            using size_type  = std::size_t;

            constexpr iterator(chonk_type const* chonk_ptr, size_type initial_index = 0) noexcept:
                m_chonk_ptr(chonk_ptr), m_index(initial_index)
            {}

            constexpr iterator(chonk_type* chonk_ptr, size_type initial_index = 0) noexcept:
                m_chonk_ptr(chonk_ptr), m_index(initial_index)
            {}

            pointer
            base_ptr() const
            {
                return m_chonk_ptr->ptr();
            }

            pointer
            ptr() const
            {
                return m_chonk_ptr->ptr(m_index);
            }

            size_type
            index() const
            {
                return m_index;
            }

            chonk_type* m_chonk_ptr;
            size_type   m_index;

            friend chonk_type;
        };
    } // namespace chonk_impl

    template <typename T, std::size_t N>
        requires(std::semiregular<T> && N <= chonk_impl::most_ts<T>())
    class chonk
    {
    public:
        using value_type      = T;
        using size_type       = std::size_t;
        using difference_type = std::ptrdiff_t;
        using reference       = T&;
        using const_reference = T const&;
        using pointer         = T*;
        using const_pointer   = T const*;

        // TODO: make this as small as possible based on the actual size of N
        using stored_size_type = std::size_t;

        using iterator               = chonk_impl::iterator<T, N>;
        using const_iterator         = chonk_impl::iterator<T const, N>;
        using reverse_iterator       = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        chonk() = default;
        ~chonk()
        {
            //
        }

        constexpr void
        assign(size_type count, T const& value)
        {
            if (count > N)
                throw std::length_error("count");

            size_type i{};

            // Handle the copies into already constructed elements
            // first
            while ((i < m_size) && (i < count))
                *ptr(i++) = value;

            if (i < count)
            {
                std::uninitialized_fill_n(ptr(i), count - m_size, value);
                m_size = count;
                return;
            }

            // At this point we know that m_size >= count. Don't bother
            // calling std::destroy_n() if the size was equal.
            auto const overage = m_size - count;
            if (overage > 0)
            {
                std::destroy_n(ptr(i), overage);
                m_size = count;
            }
        }

        template <typename InputIt, typename Sentinel>
            requires(std::input_iterator<InputIt> && std::sentinel_for<Sentinel, InputIt> &&
                     std::random_access_iterator<InputIt>)
        constexpr void
        assign(InputIt first, Sentinel last)
        {
            auto const dist = std::distance(first, last);

            if (dist > N)
                throw std::length_error("last - first");

            auto in_it  = first;
            auto out_it = ptr(0);

            if ((dist < 0) || (static_cast<size_type>(dist) <= m_size))
            {
                auto it = std::copy(first, last, out_it);
                std::destroy(it, out_it + m_size);
                m_size = dist;
                return;
            }

            auto [new_in_it, new_out_it] = std::ranges::copy_n(in_it, m_size, out_it);
            in_it                        = new_in_it;
            out_it                       = new_out_it;

            // And now handle the uninitialized territory
            std::uninitialized_copy(in_it, last, out_it);
            m_size = dist;
        }

        constexpr void
        assign(std::initializer_list<T> ilist)
        {
            if (ilist.size() > N)
                throw std::length_error("ilist");

            assign(ilist.begin(), ilist.end());
        }

        void
        clear()
        {
            if (m_size != 0)
            {
                std::destroy_n(ptr(), m_size);
                m_size = 0;
            }
        }

        constexpr size_type
        size() const
        {
            return m_size;
        }

        constexpr bool
        empty() const
        {
            return m_size == 0;
        }

        constexpr size_type
        capacity() const
        {
            return N;
        }

        constexpr size_type
        max_size() const
        {
            return N;
        }

        constexpr void
        resize(size_type count)
        {
            if (count > max_size())
            {
                throw std::length_error("count");
            }

            if (count < m_size)
            {
                std::destroy_n(ptr(count), m_size - count);
            }
            else
            {
                std::uninitialized_default_construct_n(ptr(count), count - m_size);
            }

            m_size = count;
        }

        constexpr void
        resize(size_type count, value_type const& value)
        {
            if (count > max_size())
            {
                throw std::length_error("count");
            }

            if (count < m_size)
            {
                std::destroy_n(ptr(count), m_size - count);
            }
            else
            {
                std::uninitialized_fill_n(ptr(count), count - m_size, value);
            }

            m_size = count;
        }

        constexpr reference
        at(size_type i)
        {
            if (i >= m_size)
                throw std::out_of_range("i");

            return *ptr(i);
        }

        constexpr const_reference
        at(size_type i) const
        {
            if (i >= m_size)
                throw std::out_of_range("i");

            return *ptr(i);
        }

        constexpr reference
        operator[](size_type i)
        {
            M_INTERNAL_ERROR_CHECK(i < m_size);
            return *ptr(i);
        }

        constexpr const_reference
        operator[](size_type i) const
        {
            M_INTERNAL_ERROR_CHECK(i < m_size);
            return *ptr(i);
        }

        constexpr reference
        front()
        {
            M_INTERNAL_ERROR_CHECK(m_size != 0);
            return *ptr(0);
        }

        constexpr const_reference
        front() const
        {
            M_INTERNAL_ERROR_CHECK(m_size != 0);
            return *ptr(0);
        }

        constexpr reference
        back()
        {
            M_INTERNAL_ERROR_CHECK(m_size != 0);
            return *ptr(m_size - 1);
        }

        constexpr const_reference
        back() const
        {
            M_INTERNAL_ERROR_CHECK(m_size != 0);
            return *ptr(m_size - 1);
        }

        constexpr T*
        data()
        {
            return ptr();
        }

        constexpr T const*
        data() const
        {
            return ptr();
        }

        /// <summary>
        /// Remove one eleemnt from the chonk, at
        /// the iterator position `pos`.
        /// </summary>
        /// <param name="pos"></param>
        /// <returns></returns>
        iterator
        erase(iterator pos)
        {
            M_INTERNAL_ERROR_CHECK(is_valid_iterator(pos));

            T* current = ptr(pos.index());
            T* last    = ptr(m_size);

            // You may not erase the end iterator
            M_INTERNAL_ERROR_CHECK(current != last);

            current = move_unchecked_internal(current + 1, last, current);

            std::destroy_n(current, 1);
            m_size--;

            // Since our iterators are index based, we shifted everything
            // down one and pos is still the right iterator.
            return pos;
        }

        constexpr iterator
        erase(const_iterator first,
              const_iterator last) noexcept(std::is_nothrow_move_assignable_v<value_type>)
        {
            M_INTERNAL_ERROR_CHECK(is_valid_iterator(first));
            M_INTERNAL_ERROR_CHECK(is_valid_iterator(last));

            pointer const firstptr = first.ptr();
            pointer const lastptr  = last.ptr();
            pointer const oldlast  = ptr(m_size);

            auto const first_index = first.index();

            if (firstptr != lastptr)
            {
                pointer const newlast = move_unchecked_internal(lastptr, oldlast, firstptr);
                std::destroy(newlast, oldlast);
                m_size = (newlast - ptr()) + first_index;
            }

            return iterator(this, first_index);
        }

        constexpr iterator
        erase(iterator first, iterator last) noexcept(std::is_nothrow_move_assignable_v<value_type>)
        {
            M_INTERNAL_ERROR_CHECK(is_valid_iterator(first));
            M_INTERNAL_ERROR_CHECK(is_valid_iterator(last));

            pointer const firstptr = first.ptr();
            pointer const lastptr  = last.ptr();
            pointer const oldlast  = ptr(m_size);

            auto const first_index = first.index();

            if (firstptr != lastptr)
            {
                pointer const newlast = move_unchecked_internal(lastptr, oldlast, firstptr);
                std::destroy(newlast, oldlast);
                m_size = newlast - ptr();
            }

            return iterator(this, first_index);
        }

#if 0
        iterator
        insert(const_iterator pos, T const& value)
        {
            //
        }
#endif

#if 0
        iterator
        insert(const_iterator pos, T&& value)
        {
            //
        }
#endif

#if 0
        iterator
        insert(const_iterator pos, size_type count, T const& value)
        {
            //
        }
#endif

#if 0
        template <typename InputIt, typename SentinelT>
            requires(std::sentinel_for<SentinelT, InputIt>)
        iterator
        insert(const_iterator pos, InputIt first, SentinelT last)
        {
            //
        }
#endif

#if 0
        iterator
        insert(const_iterator pos, std::initializer_list<T> ilist)
        {
            //
        }
#endif

#if 0
        template <typename... Args>
        iterator
        emplace(const_iterator pos, Args&&... args)
        {
            ...
        }
#endif

        constexpr void
        push_back(value_type const& value)
        {
            internal_emplace_at_back(value);
        }

        constexpr void
        push_back(value_type&& value)
        {
            internal_emplace_at_back(std::move(value));
        }

        template <typename... Args>
        constexpr reference
        emplace_back(Args&&... args)
        {
            return internal_emplace_at_back(std::forward<Args>(args)...);
        }

        constexpr void
        pop_back()
        {
            // Cannot call this on empty chonk
            M_INTERNAL_ERROR_CHECK(m_size > 0);

            std::destroy_n(ptr() + m_size, 1);
            m_size--;
        }

        constexpr iterator
        begin() noexcept
        {
            return iterator(this, 0);
        }

        constexpr iterator
        begin() const noexcept
        {
            return const_iterator(this, 0);
        }

        constexpr const_iterator
        cbegin() const noexcept
        {
            return const_iterator(this, 0);
        }

        constexpr iterator
        end() noexcept
        {
            return iterator(this, m_size);
        }

        constexpr const_iterator
        end() const noexcept
        {
            return const_iterator(this, m_size);
        }

        constexpr const_iterator
        cend() const noexcept
        {
            return const_iterator(this, m_size);
        }

    private:
        template <typename... Args>
        constexpr reference
        internal_emplace_at_back(Args&&... args)
        {
            if (m_size == N)
            {
                throw std::bad_alloc();
            }

            if (std::is_constant_evaluated())
            {
                std::construct_at(std::addressof(*ptr(m_size)), std::forward<Args>(args)...);
            }
            else
            {
                ::new (static_cast<void*>(std::addressof(*ptr(m_size))))
                    value_type(std::forward<Args>(args)...);
            }

            auto const result = ptr(m_size);
            m_size++;
            return *result;
        }

        template <typename InIt, typename OutIt>
        static constexpr OutIt
        move_unchecked_internal(InIt  first,
                                InIt  last,
                                OutIt dest) noexcept(std::is_nothrow_move_assignable_v<value_type>)
        {
            // the MSVC stl has logic here to test if the type is
            // memcpy assignable. Great idea for the future.

            // The phrasing here seems weird. Will check why there is a void
            // cast on the ++first some time but for now copying the
            // technique.
            for (; first != last; ++dest, (void)++first)
            {
                *dest = std::move(*first);
            }

            return dest;
        }

        T*
        iterator_to_ptr(iterator const& it)
        {
            return it.ptr();
        }

        T*
        ptr(size_type i = 0)
        {
            return reinterpret_cast<T*>(&m_bytes[sizeof(T) * i]);
        }

        T const*
        ptr(size_type i = 0) const
        {
            return reinterpret_cast<T const*>(&m_bytes[sizeof(T) * i]);
        }

        bool
        ptr_in_range(T const* check) const
        {
            auto const p = ptr();

            return (check >= p) && (check < (p + m_size));
        }

        /// <summary>
        /// Checks if a pointer is within "iterator range". Iterators are
        /// permitted to reference one after the end of the array.
        /// </summary>
        /// <param name="check"></param>
        /// <returns></returns>
        bool
        ptr_in_iter_range(T const* check) const
        {
            auto const p = ptr();

            return (check >= p) && (check <= (p + m_size));
        }

        bool
        is_valid_iterator(iterator const& it) const
        {
            return it.base_ptr() == ptr() && ptr_in_iter_range(it.ptr());
        }

        alignas(T) std::array<std::byte, sizeof(T) * N> m_bytes;

        stored_size_type m_size{};

        friend iterator;
    };
} // namespace m

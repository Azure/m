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
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/arefc_ptr/arefc_ptr.h>
#include <m/const_string/const_string.h>
#include <m/error_handling/macros.h>
#include <m/math/math.h>
#include <m/strings/compare.h>
#include <m/strings/conversion_details.h>
#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>

namespace m
{
    using namespace std::string_view_literals;

    namespace sstring_impl
    {
        template <typename TraitsT>
        struct get_comparison_category
        {
            using type = std::weak_ordering;
        };

        template <typename TraitsT>
            requires requires { typename TraitsT::comparison_category; }
        struct get_comparison_category<TraitsT>
        {
            using type = TraitsT::comparison_category;

            static_assert(m::is_any_of_v<type,
                                         std::partial_ordering,
                                         std::weak_ordering,
                                         std::strong_ordering>);
        };

        template <typename TraitsT>
        using get_comparison_category_t = get_comparison_category<TraitsT>::type;
    } // namespace sstring_impl

    template <typename CharT>
        requires(character<CharT>)
    class basic_sstring
    {
        inline static constexpr auto max_size_t = (std::numeric_limits<std::size_t>::max)();

    public:
        using char_type  = CharT;
        using value_type = CharT;
        using size_type  = std::size_t;
        using view_type  = std::basic_string_view<char_type>;
        using comparison_category_type =
            sstring_impl::get_comparison_category_t<typename view_type::traits_type>;

        static inline constexpr auto npos = view_type::npos;

        //
        // To save space in the basic_sstring proper, we use
        // only an offset and size instead of a full
        // basic_string_view<>. This type encapsulates the
        // two instead of keeping them loose since keeping
        // them loose leaves you with random size_types
        // as parameters which can be mis-ordered easily.
        //
        struct offset_and_size
        {
            size_type m_offset;
            size_type m_size;
        };

        constexpr basic_sstring() noexcept: m_offset_and_size{.m_offset = 0, .m_size = 0} {}

        basic_sstring(std::initializer_list<view_type> il)
        {
            m_arefc                    = make_basic_const_string<char_type>(il);
            m_offset_and_size.m_offset = 0;
            m_offset_and_size.m_size   = m_arefc->view().size();
            m_c_str.store(view().data(), std::memory_order_release);
            self_validate();
        }

        basic_sstring(std::span<view_type const> spn)
        {
            m_arefc                    = make_basic_const_string<char_type>(spn);
            m_offset_and_size.m_offset = 0;
            m_offset_and_size.m_size   = m_arefc->view().size();
            m_c_str.store(view().data(), std::memory_order_release);
            self_validate();
        }

        basic_sstring(view_type v)
        {
            m_arefc                    = make_basic_const_string<char_type>(v);
            m_offset_and_size.m_offset = 0;
            m_offset_and_size.m_size   = m_arefc->view().size();
            m_c_str.store(view().data(), std::memory_order_release);
            self_validate();
        }

        basic_sstring(std::basic_string<CharT> const& str):
            basic_sstring(static_cast<std::basic_string_view<CharT>>(str))
        {
            self_validate();
        }

        basic_sstring(basic_sstring const& other):
            m_arefc{other.m_arefc}, m_offset_and_size(other.m_offset_and_size)
        {
            m_c_str.store(nullptr, std::memory_order_release);
            self_validate();
        }

        /// <summary>
        /// Constructs a basic_sstring by converting from another basic_sstring with a different
        /// character type.
        ///
        /// The value must be convertible to something we can construct from via
        /// to_view_string_t<char_type>().
        /// </summary>
        /// <typeparam name="OtherCharT">The character type of the source basic_sstring. Must
        /// satisfy the character concept and must not be the same as char_type.</typeparam> <param
        /// name="other">A reference to a basic_sstring instance with a different character type to
        /// convert from.</param>
        template <typename OtherCharT>
            requires(character<OtherCharT> && !std::is_same_v<OtherCharT, char_type>)
        basic_sstring(basic_sstring<OtherCharT> const& other):
            basic_sstring(to_basic_string_view_t<char_type>(other.view()))
        {
            self_validate();
        }

        basic_sstring(basic_sstring&& other) noexcept: m_offset_and_size{.m_offset = 0, .m_size = 0}
        {
            using std::swap;
            swap(m_arefc, other.m_arefc);
            swap(m_offset_and_size, other.m_offset_and_size);
            m_c_str.store(nullptr, std::memory_order_release);

            auto t = other.m_c_str.exchange(nullptr, std::memory_order_acq_rel);
            auto u = m_c_str.exchange(t, std::memory_order_acq_rel);
            other.m_c_str.exchange(u, std::memory_order_acq_rel);
            self_validate();
        }

        ~basic_sstring()
        {
            self_validate();

            unmake_c_str();
        }

        basic_sstring&
        operator=(basic_sstring const& other)
        {
            self_validate();

            if (this != &other)
            {
                m_arefc           = other.m_arefc;
                m_offset_and_size = other.m_offset_and_size;
                unmake_c_str();
            }

            self_validate();

            return *this;
        }

        basic_sstring&
        operator=(basic_sstring&& other) noexcept
        {
            self_validate();

            using std::swap;
            swap(m_arefc, other.m_arefc);
            swap(m_offset_and_size, other.m_offset_and_size);

            auto t = other.m_c_str.exchange(nullptr, std::memory_order_acq_rel);
            auto u = m_c_str.exchange(t, std::memory_order_acq_rel);
            other.m_c_str.exchange(u, std::memory_order_acq_rel);

            self_validate();

            return *this;
        }

        basic_sstring&
        operator+=(basic_sstring const& other)
        {
            self_validate();

            basic_sstring temp = *this + other;
            using std::swap;
            swap(temp, *this);

            self_validate();

            return *this;
        }

        constexpr view_type
        view() const noexcept
        {
            view_type v{};

            if (m_arefc)
                v = view_type(m_arefc->view().data() + m_offset_and_size.m_offset,
                              m_offset_and_size.m_size);

            return v;
        }

        constexpr
        operator view_type() const noexcept
        {
            return view();
        }

        constexpr int
        compare(basic_sstring s) const
        {
            return compare(s.view());
        }

        constexpr int
        compare(view_type v) const
        {
            return view().compare(v);
        }

        constexpr int
        compare(size_type pos1, size_t count1, view_type v) const
        {
            return view().compare(pos1, count1, v);
        }

        constexpr int
        compare(size_type pos1, size_t count1, basic_sstring str) const
        {
            return view().compare(pos1, count1, str.view());
        }

        constexpr int
        compare(size_type pos1, size_type count1, view_type v, size_type pos2, size_type count2)
            const
        {
            return view().compare(pos1, count1, v, pos2, count2);
        }

        constexpr int
        compare(size_type     pos1,
                size_type     count1,
                basic_sstring str,
                size_type     pos2,
                size_type     count2) const
        {
            return view().compare(pos1, count1, str.view(), pos2, count2);
        }

        constexpr int
        compare(char_type const* s) const
        {
            return view().compare(s);
        }

        constexpr int
        compare(size_type pos1, size_type count1, char_type const* s) const
        {
            return view().compare(pos1, count1, s);
        }

        constexpr int
        compare(size_type pos1, size_type count1, char_type const* s, size_type count2) const
        {
            return view().compare(pos1, count1, s, count2);
        }

        constexpr bool
        equals(basic_sstring const& str) const noexcept
        {
            return view().compare(str.view()) == 0;
        }

        char_type const*
        c_str() const
        {
            auto local_c_str_ptr = m_c_str.load(std::memory_order_acquire);
            if (local_c_str_ptr != nullptr)
                return local_c_str_ptr;

            self_validate();

            if (!m_arefc)
                return make_c_str(nullptr, 0);

            if (m_offset_and_size.m_size == 0)
                return make_c_str(nullptr, 0);

            auto const v = m_arefc->view();

            //
            // If the end of v aligns with the end of the base_view, we can use it as
            // the c_str value. This is invariant, since basic_sstring instances are
            // immutable, so if this is true, we will store the computed c_str into
            // the m_c_str ptr into the member and leave it there into the future.
            //

            if ((m_offset_and_size.m_offset + m_offset_and_size.m_size) == v.size())
            {
                local_c_str_ptr = v.data() + m_offset_and_size.m_offset;
                m_c_str.store(local_c_str_ptr, std::memory_order_release);
                return local_c_str_ptr;
            }

            return make_c_str(v.data() + m_offset_and_size.m_offset, m_offset_and_size.m_size);
        }

        basic_sstring
        operator+(basic_sstring const& other) const
        {
            if (view().size() == 0)
                return other;

            if (other.view().size() == 0)
                return *this;

            return basic_sstring({view(), other.view()});
        }

        template <typename StringishT>
        basic_sstring
        operator+(StringishT&& other) const
        {
            auto const rhs = view_type(std::forward<StringishT>(other));

            if (view().size() == 0)
                return basic_sstring(rhs);

            if (rhs.size() == 0)
                return *this;

            return basic_sstring({view(), rhs});
        }

        friend basic_sstring
        operator+(view_type l, basic_sstring const& r)
        {
            std::initializer_list<view_type> il{l, r.view()};
            return basic_sstring(il);
        }

        basic_sstring
        substr(std::size_t start, std::size_t length = max_size_t) const
        {
            auto const v = view();
            if (start > v.size())
            {
                throw std::out_of_range("start");
            }

            auto const max_len = v.size() - start;
            if (length > max_len)
                length = max_len;

            if (length == 0)
                return basic_sstring{};

            return basic_sstring{m_arefc, offset_and_size{.m_offset = start, .m_size = length}};
        }

        basic_sstring
        left(std::size_t count) const
        {
            auto const v = view();
            if (count > v.size())
                count = v.size();
            return basic_sstring{m_arefc, offset_and_size{.m_offset = 0, .m_size = count}};
        }

        basic_sstring
        right(std::size_t count) const
        {
            auto const v = view();
            if (count > v.size())
                count = v.size();
            auto const offset = v.size() - count;
            return basic_sstring{m_arefc, offset_and_size{.m_offset = offset, .m_size = count}};
        }

        std::pair<basic_sstring, basic_sstring>
        split_at(char_type ch) const
        {
            auto const v           = view();
            auto const split_point = v.find(ch);

            if (split_point == view_type::npos)
                return std::make_pair(*this, basic_sstring{});

            return std::make_pair(substr(0, split_point), substr(split_point + 1));
        }

        std::pair<basic_sstring, basic_sstring>
        split_at(view_type view_to_find) const
        {
            auto const v           = view();
            auto const split_point = v.find(view_to_find);

            if (split_point == view_type::npos)
                return std::make_pair(*this, basic_sstring{});

            return std::make_pair(substr(0, split_point),
                                  substr(split_point + view_to_find.size()));
        }

        std::pair<basic_sstring, basic_sstring>
        split_at_first_of(view_type view_to_find) const
        {
            auto const v           = view();
            auto const split_point = v.find_first_of(view_to_find);

            if (split_point == view_type::npos)
                return std::make_pair(*this, basic_sstring{});

            return std::make_pair(substr(0, split_point), substr(split_point + 1));
        }

        bool
        contains(char_type ch) const
        {
            return view().find(ch) != view_type::npos;
        }

        std::size_t
        find_first_of(char_type ch) const
        {
            return view().find_first_of(ch);
        }

        std::size_t
        find_last_of(char_type ch) const
        {
            return view().find_last_of(ch);
        }

        std::optional<std::size_t>
        try_find_first_of(char_type ch) const
        {
            if (auto const i = find_first_of(ch); i != view_type::npos)
                return i;

            return std::nullopt;
        }

        std::optional<std::size_t>
        try_find_last_of(char_type ch) const
        {
            if (auto const i = find_last_of(ch); i != view_type::npos)
                return i;

            return std::nullopt;
        }

        bool
        empty() const noexcept
        {
            return view().size() == 0;
        }

        char_type
        operator[](std::size_t index) const
        {
            M_INTERNAL_ERROR_CHECK(index < view().size());
            return view().data()[index];
        }

        char_type
        first() const
        {
            M_INTERNAL_ERROR_CHECK(view().size() > 0);
            return view().data()[0];
        }

        char_type
        last() const
        {
            M_INTERNAL_ERROR_CHECK(view().size() > 0);
            return view().data()[view().size() - 1];
        }

        template <typename StringishT>
        constexpr bool
        operator==(StringishT&& r) const noexcept
        {
            return compare(m::to_basic_string_view_t<char_type>(std::forward<StringishT>(r))) == 0;
        }

        [[nodiscard]] constexpr comparison_category_type
        operator<=>(basic_sstring const& r) const noexcept
        {
            auto const v              = m::to_basic_string_view_t<char_type>(r);
            auto const compare_result = compare(v);

            return static_cast<comparison_category_type>(compare_result <=> 0);
        }

        [[nodiscard]] constexpr comparison_category_type
        operator<=>(view_type const& r) const noexcept
        {
            auto const v              = m::to_basic_string_view_t<char_type>(r);
            auto const compare_result = compare(v);

            return static_cast<comparison_category_type>(compare_result <=> 0);
        }

        [[nodiscard]] constexpr comparison_category_type
        operator<=>(char_type const* str) const noexcept
        {
            auto const v              = m::to_basic_string_view_t<char_type>(str);
            auto const compare_result = compare(v);

            return static_cast<comparison_category_type>(compare_result <=> 0);
        }

    private:
        void
        self_validate() const
        {
#if M_DEBUG
            if (m_arefc)
            {
                auto const v = m_arefc->view();

                // The size of the substring can't be larger than the whole
                M_INTERNAL_ERROR_CHECK(m_offset_and_size.m_size <= v.size());

                // The offset can't be beyond the whole size of the string
                M_INTERNAL_ERROR_CHECK(m_offset_and_size.m_offset <= v.size());

                // And the offset can't be beyond the size of the whole string
                // minus the size of the substring.
                M_INTERNAL_ERROR_CHECK(m_offset_and_size.m_offset <=
                                       v.size() - m_offset_and_size.m_size);

                if (v.size() - m_offset_and_size.m_size == m_offset_and_size.m_offset)
                {
                    // If the end of the substring lines up with the end of
                    // the whole string, if there is a m_c_str value, it
                    // has to be from the view.
                    auto cstr = m_c_str.load(std::memory_order_acquire);

                    if (cstr != nullptr)
                        M_INTERNAL_ERROR_CHECK(cstr == v.data() + m_offset_and_size.m_offset);
                }
            }
            else
            {
                M_INTERNAL_ERROR_CHECK(m_offset_and_size.m_size == 0);
                M_INTERNAL_ERROR_CHECK(m_offset_and_size.m_offset == 0);

                auto cstr = m_c_str.load(std::memory_order_acquire);

                if (cstr != nullptr)
                    M_INTERNAL_ERROR_CHECK(cstr[0] == 0);
            }
#endif
        }

        char_type const*
        make_c_str(char_type const* ptr, std::size_t size) const
        {
            auto cstr = m_c_str.load(std::memory_order_acquire);
            if (cstr != nullptr)
                return cstr;

            auto up = std::make_unique<char_type[]>(size + 1);

            if (size != 0)
                std::copy_n(ptr, size, up.get());

            up.get()[size] = 0;

            //
            // Now in this case the value we are storing in m_c_str is "racy" in that
            // multiple threads could have computed different copies simultaneously
            // so we need to use compare_exchange_strong() to put the value into
            // place.
            //

            char_type const* expected = nullptr;
            char_type const* desired  = up.get();

            //
            // Normally compare_exchange_*() is called in a loop because of spurious
            // failures so we do so here. In practice this should "almost never happen"
            // and the first try will either "succeed" in that we "won" the race to be
            // first to exchange into place or we "lost" to another thread and we will
            // deallocate our copy of the string.
            //

            for (;;)
            {
                if (m_c_str.compare_exchange_strong(expected, desired, std::memory_order_acq_rel))
                {
                    // We "won", so the std::unique_ptr<> needs to relinquish its control
                    // and we can proceed.
                    up.release();
                    expected = desired;
                    break;
                }

                // `expected` will be updated with the new value stored in m_c_str.
                // If it's not nullptr, then we're good.
                if (expected != nullptr)
                    break;

                // Otherwise, something unusual happened (we did not succeed in the
                // compare exchange but no other thread did either - this should
                // only happen usually with compare_exchange_weak but technically
                // it's possible - the "spurious failures") so just go around the
                // loop again.
            }

            return expected;
        }

        void
        unmake_c_str()
        {
            //
            // Note! This function uses the proper atomic operations on
            // m_c_str, but it is the caller's responsibility to ensure
            // that there is no concurrent execution.
            //
            // Specifically there are two cases where this is known to be
            // used: destruction and assignment. Only one thread of execution
            // may be performing either of these at a time.
            //
            // The .m_c_str() handles multithreaded construction of its
            // cached value as a "convenience". The overal basic_sstring<>
            // object itself is not atomic or thread safe or any of those kinds
            // of things.
            //
            auto const cstr = m_c_str.load(std::memory_order_acquire);
            if (cstr == nullptr)
                return;

            if (m_arefc)
            {
                if (cstr == m_arefc->view().data() + m_offset_and_size.m_offset)
                {
                    //
                    // This is the case that we *don't* have to do
                    // anything, so just exit early.
                    //
                    return;
                }
            }

            deallocate_c_str(cstr);
            m_c_str.store(nullptr, std::memory_order_release);
        }

        static void
        deallocate_c_str(char_type const* ptr)
        {
            char_type*                   mptr = const_cast<char_type*>(ptr);
            std::unique_ptr<char_type[]> up;
            up.reset(mptr);
        }

        basic_sstring(arefc_ptr<basic_const_string<char_type>> const& arefc,
                      offset_and_size                                 o_and_s):
            m_arefc(arefc), m_offset_and_size(o_and_s)
        {
            self_validate();
        }

        arefc_ptr<basic_const_string<char_type>> m_arefc;
        offset_and_size                          m_offset_and_size{.m_offset = 0, .m_size = 0};
        mutable std::atomic<char_type const*>    m_c_str{nullptr};
    };

    using sstring    = basic_sstring<char>;
    using wsstring   = basic_sstring<wchar_t>;
    using u8sstring  = basic_sstring<char8_t>;
    using u16sstring = basic_sstring<char16_t>;
    using u32sstring = basic_sstring<char32_t>;

    static_assert(has_view<sstring, char>);
    static_assert(has_view<wsstring, wchar_t>);
    static_assert(has_view<u8sstring, char8_t>);
    static_assert(has_view<u16sstring, char16_t>);
    static_assert(has_view<u32sstring, char32_t>);

    static_assert(has_some_view<sstring>);
    static_assert(has_some_view<wsstring>);
    static_assert(has_some_view<u8sstring>);
    static_assert(has_some_view<u16sstring>);
    static_assert(has_some_view<u32sstring>);

    static_assert(std::totally_ordered<sstring>);
    static_assert(std::totally_ordered<wsstring>);
    static_assert(std::totally_ordered<u8sstring>);
    static_assert(std::totally_ordered<u16sstring>);
    static_assert(std::totally_ordered<u32sstring>);
} // namespace m

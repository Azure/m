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

        basic_sstring() = default;

        basic_sstring(std::initializer_list<view_type> il)
        {
            m_arefc = make_basic_const_string<char_type>(il);
            m_view  = m_arefc->view();
            // Since we know that the string is null-terminated,
            // just populate the c_str now to avoid any of the
            // fuss later.
            m_c_str_v = m_arefc;
            m_c_str.store(m_view.data(), std::memory_order_release);
        }

        basic_sstring(std::span<view_type const> spn)
        {
            m_arefc = make_basic_const_string<char_type>(spn);
            m_view  = m_arefc->view();
            // Since we know that the string is null-terminated,
            // just populate the c_str now to avoid any of the
            // fuss later.
            m_c_str_v = m_arefc;
            m_c_str.store(m_view.data(), std::memory_order_release);
        }

        basic_sstring(view_type v)
        {
            m_arefc = make_basic_const_string<char_type>(v);
            m_view  = m_arefc->view();
            // Since we know that the string is null-terminated,
            // just populate the c_str now to avoid any of the
            // fuss later.
            m_c_str_v = m_arefc;
            m_c_str.store(m_view.data(), std::memory_order_release);
        }

        basic_sstring(basic_sstring const& other): m_arefc{other.m_arefc}, m_view{other.m_view}
        {
            copy_c_str_state_from_other(other);
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
            basic_sstring(to_string_view_t<char_type>(other.view()))
        {}

        basic_sstring(basic_sstring&& other) noexcept
        {
            using std::swap;
            swap(m_arefc, other.m_arefc);
            swap(m_view, other.m_view);
            swap(m_c_str_v, other.m_c_str_v);
        }

        ~basic_sstring()
        {
            // Strictly speaking, the default destructor is sufficient.
            //
            // However, there's a lot to be said for razing your state as
            // you destroy. Perhaps in the future this could be some kind
            // of compile time option or such, but at this time, it's
            // just done.
            //
            m_view = view_type{};
            m_c_str.store(nullptr, std::memory_order_relaxed);
            m_arefc.reset();
            m_c_str_v.reset();
        }

        basic_sstring&
        operator=(basic_sstring const& other)
        {
            // We take great care with the atomicity around the `.c_str()`
            // member function's mutation of state because it is intended
            // to be an observer, so callers have no expectation that they
            // will synchronize access to the basic_sstring object.
            //
            // However, at the same time, for assignment, the caller is
            // mutating the basic_sstring, so there is no need to be so
            // concerned with the state of `this`. On the other hand,
            // the state of `other` is of concern, so we need to take care
            // as we did in the copy constructor.

            if (this != &other)
            {
                // The m_v and m_view members are immutable past construction
                // and so we don't have to worry about how to read them from
                // `other`.
                m_arefc = other.m_arefc;
                m_view  = other.m_view;
                m_c_str.store(nullptr, std::memory_order_release);
                m_c_str_v.reset();
                copy_c_str_state_from_other(other);
            }

            return *this;
        }

        basic_sstring&
        operator=(basic_sstring&& other) noexcept
        {
            using std::swap;
            swap(m_arefc, other.m_arefc);
            swap(m_view, other.m_view);
            swap(m_c_str_v, other.m_c_str_v);
            m_c_str.store(nullptr, std::memory_order_release);

            return *this;
        }

        basic_sstring&
        operator+=(basic_sstring const& other)
        {
            basic_sstring temp = *this + other;
            using std::swap;
            swap(temp, *this);
            return *this;
        }

        constexpr view_type
        view() const noexcept
        {
            return m_view;
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
            if (local_c_str_ptr)
                return local_c_str_ptr;

            if (!m_arefc)
                return nullptr;

            arefc_ptr<basic_const_string<char_type>> expected; // for the compare-exchange

            // We didn't have one. Make one.
            auto [new_const_str, new_c_str_ptr] = make_c_str();

            // Attempt to swap the (possibly newly created) null terminated constant
            // string into place, with allowance for another thread racing against
            // us. If we "lose" the race, the one we had created will be deallocated
            // when new_const_str goes out of scope.
            if (m_c_str_v.compare_exchange_strong(expected, new_const_str))
            {
                m_c_str.store(new_c_str_ptr, std::memory_order_release);
                m_c_str.notify_all();
            }
            else
            {
                // We were not the thread that made the exchange.
                // Wait for the other thread to put the pointer in place.
                m_c_str.wait(nullptr, std::memory_order_release);
            }

            local_c_str_ptr = m_c_str.load(std::memory_order_acquire);

            M_INTERNAL_ERROR_CHECK(local_c_str_ptr != nullptr);
            return local_c_str_ptr;
        }

        basic_sstring
        operator+(basic_sstring const& other) const
        {
            if (view().size() == 0)
                return other;

            if (other.view().size() == 0)
                return *this;

            return basic_sstring({m_view, other.view()});
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

            return basic_sstring({m_view, rhs});
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

            return basic_sstring{m_arefc, view_type(v.data() + start, length)};
        }

        basic_sstring
        left(std::size_t count) const
        {
            auto const v = view();
            if (count > v.size())
                count = v.size();
            return basic_sstring(m_arefc, view_type(v.data(), count));
        }

        basic_sstring
        right(std::size_t count) const
        {
            auto const v = view();
            if (count > v.size())
                count = v.size();
            auto const offset = v.size() - count;
            return basic_sstring(m_arefc, view_type(v.data() + offset, count));
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
            M_INTERNAL_ERROR_CHECK(index < m_view.size());
            return m_view.data()[index];
        }

        char_type
        first() const
        {
            M_INTERNAL_ERROR_CHECK(m_view.size() > 0);
            return m_view.data()[0];
        }

        char_type
        last() const
        {
            M_INTERNAL_ERROR_CHECK(m_view.size() > 0);
            return m_view.data()[m_view.size() - 1];
        }

        template <typename StringishT>
        constexpr bool
        operator==(StringishT&& r) const noexcept
        {
            return compare(m::to_string_view_t<char_type>(std::forward<StringishT>(r))) == 0;
        }

        [[nodiscard]] constexpr comparison_category_type
        operator<=>(basic_sstring const& r) const noexcept
        {
            auto const v              = m::to_string_view_t<char_type>(r);
            auto const compare_result = compare(v);

            return static_cast<comparison_category_type>(compare_result <=> 0);
        }

        [[nodiscard]] constexpr comparison_category_type
        operator<=>(view_type const& r) const noexcept
        {
            auto const v              = m::to_string_view_t<char_type>(r);
            auto const compare_result = compare(v);

            return static_cast<comparison_category_type>(compare_result <=> 0);
        }

        [[nodiscard]] constexpr comparison_category_type
        operator<=>(char_type const* str) const noexcept
        {
            auto const v              = m::to_string_view_t<char_type>(str);
            auto const compare_result = compare(v);

            return static_cast<comparison_category_type>(compare_result <=> 0);
        }

    private:
        void
        copy_c_str_state_from_other(basic_sstring const& other)
        {
            if (auto const other_ptr = other.m_c_str.load(std::memory_order_acquire);
                other_ptr != nullptr)
            {
                m_c_str_v = other.m_c_str_v;
                m_c_str.store(other_ptr, std::memory_order_release);
            }
        }

        std::pair<arefc_ptr<basic_const_string<char_type>>, char_type const*>
        make_c_str() const
        {
            auto const v_view = m_arefc->view();

            // If the end of `this`'s view coincides with the end of m_v's
            // view, then we don't need to allocate a new basic_const_string<>.
            //

            if (v_view.data() + v_view.size() == m_view.data() + m_view.size())
            {
                return std::make_pair(m_arefc, m_view.data());
            }

            // We must allocate a new basic_const_string<>
            //

            auto new_string = make_basic_const_string<char_type>(m_view);
            return std::make_pair(new_string, new_string->view().data());
        }

        basic_sstring(arefc_ptr<basic_const_string<char_type>> const& arefc, view_type view):
            m_arefc(arefc), m_view(view)
        {
            // Populate c_str() if possible
            // c_str();
        }

        arefc_ptr<basic_const_string<char_type>>         m_arefc;
        std::basic_string_view<char_type>                m_view;
        mutable arefc_ptr<basic_const_string<char_type>> m_c_str_v;
        mutable std::atomic<char_type const*>            m_c_str;
    };

    using sstring    = basic_sstring<char>;
    using wsstring   = basic_sstring<wchar_t>;
    using u8sstring  = basic_sstring<char8_t>;
    using u16sstring = basic_sstring<char16_t>;
    using u32sstring = basic_sstring<char32_t>;

    template <typename CharT>
        requires(m::character<CharT>)
    struct string_conversion_helper<basic_sstring<CharT>, CharT>
    {
        using sstring_t     = basic_sstring<CharT>;
        using string_t      = std::basic_string<CharT>;
        using string_view_t = std::basic_string_view<CharT>;

        constexpr static string_view_t
        xlate_to_view(sstring_t&& str) noexcept
        {
            return str.view();
        }

        constexpr static string_view_t
        xlate_to_view(sstring_t const& str) noexcept
        {
            return str.view();
        }

        static string_t
        xlate_to_string(sstring_t&& str)
        {
            return string_t(str.view());
        }

        static string_t
        xlate_to_string(sstring_t const& str)
        {
            return string_t(str.view());
        }
    };

    template <typename CharT>
        requires(m::character<CharT>)
    struct string_conversion_helper<std::optional<basic_sstring<CharT>>, CharT>
    {
        using sstring_t     = basic_sstring<CharT>;
        using string_t      = std::basic_string<CharT>;
        using string_view_t = std::basic_string_view<CharT>;

        constexpr static std::optional<string_view_t>
        xlate_to_view(std::optional<sstring_t> const& str) noexcept
        {
            if (!str.has_value())
                return std::nullopt;

            return str.value().view();
        }

        static std::optional<string_t>
        xlate_to_string(std::optional<sstring_t> const& str)
        {
            if (!str.has_value())
                return std::nullopt;

            return string_t(str.value().view());
        }
    };

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

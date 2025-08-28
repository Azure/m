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
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>

namespace m
{
    template <typename CharT>
        requires(character<CharT>)
    class basic_sstring
    {
        inline static constexpr auto max_size_t = (std::numeric_limits<std::size_t>::max)();

    public:
        using char_type  = CharT;
        using value_type = CharT;
        using view_type  = std::basic_string_view<char_type>;

        basic_sstring() = default;
        basic_sstring(std::initializer_list<view_type> il)
        {
            m_v    = make_basic_const_string<char_type>(il);
            m_view = m_v->view();
            // Since we know that the string is null-terminated,
            // just populate the c_str now to avoid any of the
            // fuss later.
            m_c_str_v = m_v;
            m_c_str.store(m_view.data(), std::memory_order_release);
        }

        basic_sstring(view_type v)
        {
            m_v    = make_basic_const_string<char_type>(v);
            m_view = m_v->view();
            // Since we know that the string is null-terminated,
            // just populate the c_str now to avoid any of the
            // fuss later.
            m_c_str_v = m_v;
            m_c_str.store(m_view.data(), std::memory_order_release);
        }

        basic_sstring(basic_sstring const& other): m_v{other.m_v}, m_view{other.m_view}
        {
            copy_c_str_state_from_other(other);
        }

        basic_sstring(basic_sstring&& other) noexcept
        {
            using std::swap;
            swap(m_v, other.m_v);
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
            m_v.reset();
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
                m_v    = other.m_v;
                m_view = other.m_view;
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
            swap(m_v, other.m_v);
            swap(m_view, other.m_view);
            swap(m_c_str_v, other.m_c_str_v);

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

        view_type
        view() const
        {
            return m_view;
        }

        operator view_type() const { return view(); }

        char_type const*
        c_str() const
        {
            auto local_c_str_ptr = m_c_str.load(std::memory_order_acquire);
            if (local_c_str_ptr)
                return local_c_str_ptr;

            if (!m_v)
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

            return basic_sstring{m_v, view_type(v.data() + start, length)};
        }

        basic_sstring
        left(std::size_t count) const
        {
            auto const v = view();
            if (count > v.size())
                count = v.size();
            return basic_sstring(m_v, view_type(v.data(), count));
        }

        basic_sstring
        right(std::size_t count) const
        {
            auto const v = view();
            if (count > v.size())
                count = v.size();
            auto const offset = v.size() - count;
            return basic_sstring(m_v, view_type(v.data() + offset, count));
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
        operator==(basic_sstring const& other) const
        {
            return view() == other.view();
        }

        bool
        operator==(view_type other) const
        {
            return view() == other;
        }

        friend bool
        operator==(view_type l, basic_sstring const& r)
        {
            return l == r.view();
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
            //
            auto const v_view = m_v->view();

            // If the end of `this`'s view coincides with the end of m_v's
            // view, then we don't need to allocate a new basic_const_string<>.
            //

            if (v_view.data() + v_view.size() == m_view.data() + m_view.size())
            {
                return std::make_pair(m_v, m_view.data());
            }

            // We must allocate a new basic_const_string<>
            //

            auto new_string = make_basic_const_string<char_type>(m_view);
            return std::make_pair(new_string, new_string->view().data());
        }

        basic_sstring(arefc_ptr<basic_const_string<char_type>> const& aptr, view_type view):
            m_v(aptr), m_view(view)
        {
            // Populate c_str() if possible
            // c_str();
        }

        arefc_ptr<basic_const_string<char_type>>         m_v;
        std::basic_string_view<char_type>                m_view;
        mutable arefc_ptr<basic_const_string<char_type>> m_c_str_v;
        mutable std::atomic<char_type const*>            m_c_str;
    };

    using sstring    = basic_sstring<char>;
    using wsstring   = basic_sstring<wchar_t>;
    using u8sstring  = basic_sstring<char8_t>;
    using u16sstring = basic_sstring<char16_t>;
    using u32sstring = basic_sstring<char32_t>;

} // namespace m

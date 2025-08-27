// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm> // for rotate, equals, move_backwards, ...
#include <array>
#include <compare>
#include <concepts> // for lots...
#include <cstddef>  // for size_t
#include <cstdint>  // for fixed-width integer types
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <stdio.h> // for assertion diagnostics
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/arc_ptr/arc_ptr.h>
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
        basic_sstring(basic_sstring const& other):
            m_v{other.m_v}, m_view{other.m_view}, m_c_str_v{other.m_c_str_v}
        {}
        basic_sstring(basic_sstring&& other) noexcept
        {
            using std::swap;
            swap(m_v, other.m_v);
            swap(m_view, other.m_view);
            swap(m_c_str_v, other.m_c_str_v);
        }
        ~basic_sstring() = default;

        view_type
        view() const
        {
            return m_view;
        }

        char_type const*
        c_str() const
        {
            auto local_c_str_ptr = m_c_str.load(std::memory_order_relaxed);
            if (local_c_str_ptr)
                return local_c_str_ptr;

            if (!m_v)
                return nullptr;

            arc_ptr<basic_const_string<char_type>> expected; // for the compare-exchange

            // We didn't have one. Make one.
            auto [new_const_str, new_c_str_ptr] = make_c_str();

            // Attempt to swap the (possibly newly created) null terminated constant
            // string into place, with allowance for another thread racing against
            // us. If we "lose" the race, the one we had created will be deallocated
            // when new_const_str goes out of scope.
            if (m_c_str_v.compare_exchange_strong(expected, new_const_str))
            {
                m_c_str.store(new_c_str_ptr, std::memory_order_relaxed);
                m_c_str.notify_all();
            }
            else
            {
                // We were not the thread that made the exchange.
                // Wait for the other thread to put the pointer in place.
                m_c_str.wait(nullptr, std::memory_order_relaxed);
            }

            local_c_str_ptr = m_c_str.load(std::memory_order_relaxed);

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

        bool
        operator==(basic_sstring const& other) const
        {
            return view() == other.view();
        }

    private:
        std::pair<arc_ptr<basic_const_string<char_type>>, char_type const*>
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
            return std::make_pair<new_string, new_string->view().data()>;
        }

        arc_ptr<basic_const_string<char_type>>         m_v;
        std::basic_string_view<char_type>              m_view;
        mutable arc_ptr<basic_const_string<char_type>> m_c_str_v;
        mutable std::atomic<char_type const*>          m_c_str;
    };

    using sstring    = basic_sstring<char>;
    using wsstring   = basic_sstring<wchar_t>;
    using u8sstring  = basic_sstring<char8_t>;
    using u16sstring = basic_sstring<char16_t>;
    using u32sstring = basic_sstring<char32_t>;

} // namespace m

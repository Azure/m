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

#include <m/error_handling/macros.h>
#include <m/math/math.h>
#include <m/utility/pointers.h>

namespace m
{
    namespace sstring_impl
    {
        template <typename CharT>
        class ssv
        {
        public:
            using char_type    = CharT;
            using view_type    = std::basic_string_view<char_type>;
            using string_type  = std::basic_string<char_type>;
            using counted_type = std::shared_ptr<string_type>;

            ssv() = default;

            ssv(view_type v): m_sp{}
            {
                if (v.size() != 0)
                {
                    string_type s;
                    s.resize_and_overwrite(v.size() + 1,
                                           [v](char_type* p, std::size_t cnt) noexcept {
                                               M_INTERNAL_ERROR_CHECK(cnt >= (v.size() + 1));
                                               std::copy_n(v.begin(), v.size(), p);
                                               p[v.size()] = 0;
                                               return v.size() + 1;
                                           });
                    m_v  = view_type(s.data(), s.size() - 1);
                    m_sp = std::make_shared<string_type>(std::move(s));
                }
            }

            ssv(ssv const& other): m_sp(other.m_sp), m_v{}
            {
                if (m_sp)
                {
                    m_v = view_type(m_sp->data(), m_sp->size() - 1);
                }
            }

            ssv(ssv&& other) noexcept: m_v{}
            {
                using std::swap;
                swap(m_sp, other.m_sp);
                swap(m_v, other.m_v);
            }

            ~ssv() = default;

            ssv&
            operator=(ssv const& other)
            {
                if (this != &other)
                {
                    m_sp = other.m_sp;
                    m_v  = other.m_v;
                }
                return *this;
            }

            ssv&
            operator=(ssv&& other)
            {
                if (this != &other)
                {
                    using std::swap;
                    swap(m_sp, other.m_sp);
                    swap(m_v, other.m_v);
                }

                return *this;
            }

            bool
            operator==(ssv const& other) const
            {
                return m_v == other.m_v;
            }

            void
            swap(ssv& other) noexcept
            {
                using std::swap;
                swap(m_sp, other.m_sp);
                swap(m_v, other.m_v);
            }

            explicit
            operator bool() const
            {
                return static_cast<bool>(m_sp);
            }

            bool
            operator!() const
            {
                return !static_cast<bool>(*this);
            }

            view_type
            view() const noexcept
            {
                return m_v;
            }

            char_type const*
            c_str() const
            {
                // THIS IS WRONG! c_str() may force a relocation to a new string
                // if the current view is not the end of the current string.
                char_type const* return_value{};

                if (m_sp)
                    return_value = m_sp->c_str();

                return return_value;
            }

            static ssv
            concatenate(std::initializer_list<ssv> il)
            {
                return ssv(il);
            }

        private:
            ssv(string_type&& str, view_type v):
                m_sp(std::make_shared<string_type>(std::move(str))), m_v(v)
            {
                // This constructor is private because we assume our caller
                // knows what they are doing, e.g. they have constructed
                // a large string, with a trailing null intentionally.
                //
                // We require them to pass in the view mostly as a formality
                // for them to prove that they know what they are doing and
                // we can validate that fact here.
                //
                M_INTERNAL_ERROR_CHECK(v.data() == m_sp->data());
                M_INTERNAL_ERROR_CHECK(v.size() == m_sp->size() - 1);
            }

            ssv(std::initializer_list<ssv> const& il)
            {
                std::size_t len{};

                for (auto const& e: il)
                    len = m::math::add(len, e.view().size(), std::size_t{});

                len = m::math::add(len, 1, std::size_t{});

                m_sp = std::make_shared<string_type>();

                m_sp->resize_and_overwrite(len, [&](char_type* ptr, std::size_t buflen) {
                    M_INTERNAL_ERROR_CHECK(buflen >= len);
                    char_type* c = ptr; // "c" for "cursor"
                    for (auto const& e: il)
                    {
                        std::copy_n(e.view().data(), e.view().size(), c);
                        c += e.view().size();
                    }
                    *c++ = 0;
                    return c - ptr;
                });

                // We don't know what really came in to the concatenate, but
                // we MUST have allocated space for the trailing null.
                M_INTERNAL_ERROR_CHECK(m_sp->size() > 0);

                m_v = view_type(m_sp->data(), m_sp->size() - 1);
            }

            // Remember that the value stored is always one
            // more character in length than the actual value.
            // The empty string is represented by an empty
            // counted type.
            counted_type m_sp;
            view_type    m_v;
        };
    } // namespace sstring_impl

    template <typename CharT>
    class basic_sstring
    {
    public:
        using char_type = CharT;
        using view_type = std::basic_string_view<char_type>;

        basic_sstring() = default;
        basic_sstring(view_type v): m_v(v) {}
        basic_sstring(basic_sstring const& other): m_v(other.m_v) {}
        basic_sstring(basic_sstring&& other) noexcept
        {
            using std::swap;
            swap(m_v, other.m_v);
        }
        ~basic_sstring() = default;

        view_type
        view() const
        {
            return m_v.view();
        }

        char_type const*
        c_str() const
        {
            // THIS IS WRONG - c_str() may need to switch to a new
            // string if m_v()'s view is not at the end of the string.
            // (and thus would not be null terminated)
            return m_v.c_str();
        }

        basic_sstring
        operator+(basic_sstring const& other) const
        {
            if (!m_v)
                return other.m_v;

            if (!other.m_v)
                return m_v;

            return basic_sstring(value_type::concatenate({m_v, other.m_v}));
        }

        bool
        operator==(basic_sstring const& other) const
        {
            return m_v == other.m_v;
        }

    private:
        using value_type = sstring_impl::ssv<char_type>;

        basic_sstring(value_type value): m_v(std::move(value)) {}

        value_type m_v;
    };

    using sstring    = basic_sstring<char>;
    using wsstring   = basic_sstring<wchar_t>;
    using u8sstring  = basic_sstring<char8_t>;
    using u16sstring = basic_sstring<char16_t>;
    using u32sstring = basic_sstring<char32_t>;

} // namespace m

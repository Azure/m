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
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <m/error_handling/macros.h>
#include <m/math/math.h>
#include <m/arc_ptr/arc_ptr.h>
#include <m/utility/concepts.h>
#include <m/utility/pointers.h>

namespace m
{
    namespace basic_const_string_impl
    {
        //
    } // namespace basic_const_string_impl

    template <typename CharT>
        requires(m::character<CharT>)
    class basic_const_string;

    template <typename CharT, typename StringishT>
        requires(m::character<CharT>)
    m::arc_ptr<basic_const_string<CharT>>
    make_basic_const_string(StringishT&& s);

    template <typename CharT, typename StringishT>
        requires(m::character<CharT>)
    m::arc_ptr<basic_const_string<CharT>>
    make_basic_const_string(std::initializer_list<StringishT> il);

    template <typename CharT>
        requires(m::character<CharT>)
    class basic_const_string
    {
    public:
        using char_type   = CharT;
        using view_type   = std::basic_string_view<char_type>;
        using traits_type = typename view_type::traits_type;

        basic_const_string()  = default;
        ~basic_const_string() = default;

        view_type
        view() const
        {
            return view_type(get_buffer_ptr(), m_size);
        }

        /// <summary>
        /// Returns a basic_string_view<> for this const_string.
        ///
        /// Should be used almost synonymously with this object.
        /// </summary>
        explicit
        operator view_type() const
        {
            return view();
        }

        char_type const*
        c_str() const
        {
            return get_buffer_ptr();
        }

        bool
        operator==(basic_const_string const& other) const
        {
            return view() == other.view();
        }

        std::strong_ordering
        operator<=>(basic_const_string const& r) const
        {
            return operator<=>(this->view(), r.view());
        }

    private:
        basic_const_string(view_type value): m_size(value.size())
        {
            auto const buffer = get_buffer_ptr();

            std::copy_n(value.begin(), m_size, buffer);
            buffer[m_size] = 0;
        }

        basic_const_string(std::initializer_list<view_type> il): m_size(0)
        {
            auto const buffer = get_buffer_ptr();
            auto       cursor = buffer;

            for (auto const& str: il)
            {
                std::copy_n(str.begin(), str.size(), cursor);
                cursor += str.size();
            }

            *cursor = 0;

            m_size = cursor - buffer;
        }

        char_type*
        get_buffer_ptr()
        {
            auto const psize        = &m_size;
            auto const psize_plus_1 = psize + 1;
            return reinterpret_cast<char_type*>(psize_plus_1);
        }

        char_type const*
        get_buffer_ptr() const
        {
            auto const psize        = &m_size;
            auto const psize_plus_1 = psize + 1;
            return reinterpret_cast<char_type const*>(psize_plus_1);
        }

        //
        // Functions to help make_const_string work its magic:
        //

        static std::size_t
        stringish_size(view_type const& v)
        {
            return v.size();
        }

        std::size_t m_size;

        // The make function is a friend so it can refer into this class to
        // use the private functions to compute length, allocate, construct.
        //
        template <typename U, typename S>
            requires(m::character<U>)
        friend m::arc_ptr<basic_const_string<U>>
        make_basic_const_string(S&& s);

        template <typename U, typename S>
            requires(m::character<U>)
        friend m::arc_ptr<basic_const_string<U>>
        make_basic_const_string(std::initializer_list<S> il);
    };

    template <typename CharT, typename StringishT>
        requires(m::character<CharT>)
    m::arc_ptr<basic_const_string<CharT>>
    make_basic_const_string(StringishT&& str)
    {
        // We received "s" by rvalue-reference, but we cannot
        // std::forward<>() it or std::move() it because we need
        // to use it multiple times, so be careful even though it
        // would seem natural to forward or move it.

        auto const str_chars_needed =
            m::math::add(basic_const_string<CharT>::stringish_size(str), 1, std::size_t{});

        auto const str_bytes_needed =
            m::math::multiply(str_chars_needed, sizeof(CharT), std::size_t{});

        auto const bytes_needed =
            m::math::add(str_bytes_needed, sizeof(basic_const_string<CharT>), std::size_t{});

        return m::make_arc_ex<basic_const_string<CharT>>(
            bytes_needed,
            nullptr,
            [](std::span<std::byte> s, StringishT const& str2) {
                return ::new (s.data()) basic_const_string<CharT>(str2);
            },
            str);
    }

    template <typename CharT, typename StringishT>
        requires(m::character<CharT>)
    m::arc_ptr<basic_const_string<CharT>>
    make_basic_const_string(std::initializer_list<StringishT> il)
    {
        std::size_t chars_needed{};

        for (auto const& str: il)
        {
            chars_needed = m::math::add(chars_needed,
                                        basic_const_string<CharT>::stringish_size(str),
                                        decltype(chars_needed){});
        }

        chars_needed = m::math::add(chars_needed, 1, decltype(chars_needed){});

        auto const str_bytes_needed = m::math::multiply(chars_needed, sizeof(CharT), std::size_t{});

        auto const bytes_needed =
            m::math::add(str_bytes_needed, sizeof(basic_const_string<CharT>), std::size_t{});

        return m::make_arc_ex<basic_const_string<CharT>>(
            bytes_needed,
            nullptr,
            [](std::span<std::byte> s, std::initializer_list<StringishT> il2) {
                return ::new (s.data()) basic_const_string<CharT>(il2);
            },
            il);
    }

    using const_string    = basic_const_string<char>;
    using wconst_string   = basic_const_string<wchar_t>;
    using u8const_string  = basic_const_string<char8_t>;
    using u16const_string = basic_const_string<char16_t>;
    using u32const_string = basic_const_string<char32_t>;

    template <typename StringishT>
    m::arc_ptr<const_string>
    make_const_string(StringishT&& str)
    {
        return make_basic_const_string<char>(std::forward<StringishT>(str));
    }

    template <typename StringishT>
    m::arc_ptr<const_string>
    make_const_string(std::initializer_list<StringishT> il)
    {
        return make_basic_const_string<char>(il);
    }

    template <typename StringishT>
    m::arc_ptr<wconst_string>
    make_wconst_string(StringishT&& str)
    {
        return make_basic_const_string<wchar_t>(std::forward<StringishT>(str));
    }

    template <typename StringishT>
    m::arc_ptr<wconst_string>
    make_wconst_string(std::initializer_list<StringishT> il)
    {
        return make_basic_const_string<wchar_t>(il);
    }

} // namespace m

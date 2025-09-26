// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <initializer_list>
#include <mutex>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <version>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/inplace_vector/inplace_vector.h>
#include <m/math/math.h>
#include <m/utility/pointers.h>
#include <m/utility/smallest_size.h>

//
// As a general design policy, we do our best not to touch the overflow object until/
// unless we need to. We would like to afford the overflow object the ability to not
// manifest itself until the latest possible moment, so until the inplace buffer is
// at capacity, do not call into the overflow object.
//
// This leads to what would seem like superfluous avoidance of using trivial member
// functions on something that you may "know" is just getting the size() of a
// std::vector<char>. However for the base string buffer type, we want to avoid these
// assumptions and not touch the overflow until and unless necessary. We hope to never
// touch the overflow for the user's sake.
//

namespace m
{
    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    class basic_string_buffer_base
    {
    public:
        static inline constexpr std::size_t inline_value_count = NInlineValueCount;
        using value_type                                       = CharT;
        using derived_most_string_buffer_type                  = DerivedMostStringBufferT;

        using this_type =
            basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>;

        using char_traits      = std::char_traits<value_type>;
        using string_type      = std::basic_string<value_type>;
        using string_view_type = std::basic_string_view<value_type>;
        using czstring_type    = value_type const*;
        using span_type        = std::span<value_type const>;
        using size_type        = std::size_t;

        size_type
        size() const noexcept;

        m::not_null<czstring_type>
        c_str();

        void
        append(string_view_type view);

        void
        append(span_type spn);

        void
        assign(string_view_type view);

        void
        assign(span_type spn);

        void
        assign(std::initializer_list<string_view_type> il);

        void
        assign(std::initializer_list<span_type> il);

        void
        assign(basic_string_buffer_base const& other);

        void
        clear();

        void
        push_back(value_type const& v);

        void
        push_back(value_type&& v);

    protected:
        constexpr basic_string_buffer_base() = default;

        constexpr basic_string_buffer_base(basic_string_buffer_base&& other) noexcept;

        basic_string_buffer_base(basic_string_buffer_base const& other);

        ~basic_string_buffer_base() = default;

        basic_string_buffer_base&
        operator=(basic_string_buffer_base const& other);

        basic_string_buffer_base&
        operator=(basic_string_buffer_base&& other) noexcept;

        void
        swap(basic_string_buffer_base& other) noexcept;

        constexpr bool
        exceeds_inplace_vector() const noexcept;

        size_type
        overflow_size() const noexcept;

        constexpr span_type
        inplace_vector_span() const noexcept;

        std::optional<std::span<span_type>>
        get_overflow_spans(std::size_t starting_offset, std::span<span_type> spnspn) const noexcept
        {
            return static_cast<DerivedMostStringBufferT const*>(this)
                ->m_overflow_provider.get_overflow_spans(starting_offset, spnspn);
        }

        m::inplace_vector<value_type, inline_value_count> m_inplace_vector;
        std::unique_ptr<value_type const>                 m_c_str;
    };

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::
        basic_string_buffer_base(basic_string_buffer_base const& other):
        m_inplace_vector(other.m_inplace_vector)
    {}

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    constexpr basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::
        basic_string_buffer_base(basic_string_buffer_base&& other) noexcept
    {
        using std::swap;
        swap(m_inplace_vector, other.m_inplace_vector);
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>&
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::operator=(
        basic_string_buffer_base const& other)
    {
        m_inplace_vector = other.m_inplace_vector;
        return *this;
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>&
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::operator=(
        basic_string_buffer_base&& other) noexcept
    {
        using std::swap;
        swap(m_inplace_vector, other.m_inplace_vector);
        return *this;
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::swap(
        basic_string_buffer_base& other) noexcept
    {
        using std::swap;
        swap(m_inplace_vector, other.m_inplace_vector);
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    m::not_null<typename basic_string_buffer_base<CharT,
                                                  NInlineValueCount,
                                                  DerivedMostStringBufferT>::value_type const*>
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::c_str()
    {
        auto const len = size() + 1;
        auto       ptr = std::make_unique<value_type[]>(len);
        auto [in, out] = std::ranges::copy(m_inplace_vector, ptr.get());
        if (exceeds_inplace_vector())
        {
            out = static_cast<DerivedMostStringBufferT*>(this)->append_overflow_to_iterator(out);
        }

        *out = 0;

        m_c_str.reset(ptr.release());
        return m_c_str.get();
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::append(
        string_view_type v)
    {
        append(span_type(v.data(), v.size()));
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::append(
        span_type spn)
    {
        auto const inplace_remaining = m_inplace_vector.capacity() - m_inplace_vector.size();

        auto inplace_portion = std::min(spn.size(), inplace_remaining);

        if (inplace_portion != 0)
        {
            auto inplace_span = spn.subspan(0, inplace_portion);
            m_inplace_vector.append_range(inplace_span);

            // Move the span on past what we managed to store in the inplace area
            spn = spn.subspan(inplace_portion);
        }

        // If there's anything left after stashing into the inplace vector,
        // add it to the overflow by downcasting (safely!) and passing the span
        // of characters to the derived type.
        if (spn.size() != 0)
        {
            static_cast<DerivedMostStringBufferT*>(this)->append_to_overflow(spn);
        }
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::assign(
        span_type spn)
    {
        m_inplace_vector.assign_range(spn.subspan(0, std::min(spn.size(), inline_value_count)));

        if (spn.size() > inline_value_count)
        {
            static_cast<DerivedMostStringBufferT*>(this)->assign_to_overflow(
                spn.subspan(inline_value_count));
        }
        else if (exceeds_inplace_vector())
        {
            static_cast<DerivedMostStringBufferT*>(this)->clear_overflow();
        }
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::assign(
        string_view_type v)
    {
        assign(span_type(v.data(), v.size()));
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::assign(
        std::initializer_list<string_view_type> il)
    {
        if (il.size() == 0)
        {
            clear();
            return;
        }

        auto       it  = il.begin();
        auto const end = il.end();

        assign(*it++);

        while (it != end)
            append(*it++);
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::assign(
        std::initializer_list<span_type> il)
    {
        if (il.size() == 0)
        {
            clear();
            return;
        }

        auto       it  = il.begin();
        auto const end = il.end();

        assign(*it++);

        while (it != end)
            append(*it++);
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::assign(
        basic_string_buffer_base const& other)
    {
        auto const other_inplace_span = other.inplace_vector_span();

        // Remember don't touch the overflow provider unless you have to.
        if (!other.exceeds_inplace_vector())
        {
            assign(other_inplace_span);
        }
        else
        {
            std::array<span_type, 4> spnarray;
            auto                     spnspn = std::span(spnarray.data(), spnarray.max_size());

            std::size_t idx{};

            auto        other_overflow_spans =
                static_cast<DerivedMostStringBufferT const*>(&other)->get_overflow_spans(idx, spnspn);

            if (!other_overflow_spans.has_value())
            {
                clear();
                return;
            }

            std::span<span_type> spns = other_overflow_spans.value();

            switch (spns.size())
            {
                case 0:
                {
                    clear();
                    return;
                }

                case 1:
                {
                    assign(std::initializer_list<span_type>{other_inplace_span, spns[0]});
                    return;
                }

                case 2:
                {
                    assign(std::initializer_list<span_type>{other_inplace_span, spns[0], spns[1]});
                    return;
                }

                case 3:
                {
                    assign(std::initializer_list<span_type>{
                        other_inplace_span, spns[0], spns[1], spns[2]});
                    return;
                }

                case 4:
                {
                    assign(std::initializer_list<span_type>{
                        other_inplace_span, spns[0], spns[1], spns[2], spns[3]});
                    return;
                }

                default:
                {
                    M_INTERNAL_ERROR_CHECK(spns.size() > 4);
                    assign(std::initializer_list<span_type>{
                        other_inplace_span,
                                                 spns[0],
                                                 spns[1], spns[2], spns[3]});

                    for (std::size_t i = 4; i < spns.size(); i++)
                        append(spns[i]);

                    idx = spns.size();

                    break;
                }
            }

            for (;;)
            {
                other_overflow_spans =
                    static_cast<DerivedMostStringBufferT const*>(&other)->get_overflow_spans(
                        idx, spnspn);

                if (!other_overflow_spans.has_value())
                    break;

                spns = other_overflow_spans.value();

                for (auto const& e : spns)
                    append(e);

                idx += spns.size();
            }
        }
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::push_back(
        value_type const& v)
    {
        if (m_inplace_vector.capacity() != m_inplace_vector.size())
        {
            m_inplace_vector.push_back(v);
        }
        else
        {
            static_cast<DerivedMostStringBufferT*>(this)->push_back_to_overflow(v);
        }
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::push_back(
        value_type&& v)
    {
        if (m_inplace_vector.capacity() != m_inplace_vector.size())
        {
            m_inplace_vector.push_back(std::move(v));
        }
        else
        {
            static_cast<DerivedMostStringBufferT*>(this)->push_back_to_overflow(std::move(v));
        }
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::size_type
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::overflow_size()
        const noexcept
    {
        return static_cast<DerivedMostStringBufferT const*>(this)->size_of_overflow();
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::size_type
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::size()
        const noexcept
    {
        if (!exceeds_inplace_vector())
            return m_inplace_vector.size();

        return m::math::add(m_inplace_vector.size(), overflow_size(), std::size_t{});
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    void
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::clear()
    {
        if (exceeds_inplace_vector())
            static_cast<DerivedMostStringBufferT*>(this)->clear_overflow();

        m_inplace_vector.clear();
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    constexpr bool
    basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::
        exceeds_inplace_vector() const noexcept
    {
        if (m_inplace_vector.size() < m_inplace_vector.capacity())
            return false;

        return overflow_size() != 0;
    }

    template <typename CharT, std::size_t NInlineValueCount, typename DerivedMostStringBufferT>
    constexpr basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::
        span_type
        basic_string_buffer_base<CharT, NInlineValueCount, DerivedMostStringBufferT>::
            inplace_vector_span() const noexcept
    {
        return span_type(m_inplace_vector.data(), m_inplace_vector.size());
    }

} // namespace m

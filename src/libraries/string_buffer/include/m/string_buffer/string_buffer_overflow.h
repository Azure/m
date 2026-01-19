// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <version>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/inplace_vector/inplace_vector.h>
#include <m/utility/pointers.h>
#include <m/utility/smallest_size.h>

#include <m/string_buffer/string_buffer_base.h>
#include <m/string_buffer/string_buffer_overflow_provider.h>

namespace m
{
    template <typename CharT,
              std::size_t NInlineValueCount = 64,
              typename OverflowProviderT    = basic_string_buffer_overflow_provider<CharT>>
        requires(std::is_nothrow_destructible_v<CharT>)
    class basic_string_buffer_internal_overflow :
        public basic_string_buffer_base<
            CharT,
            NInlineValueCount,
            basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>>
    {
    public:
        using base_type = basic_string_buffer_base<
            CharT,
            NInlineValueCount,
            basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>>;

        using this_type =
            basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>;

        using overflow_provider_type = OverflowProviderT;

        using base_type::inline_value_count;
        using typename base_type::char_traits;
        using typename base_type::czstring_type;
        using typename base_type::span_type;
        using typename base_type::string_type;
        using typename base_type::string_view_type;
        using typename base_type::value_type;

        static_assert(inline_value_count == NInlineValueCount);
        static_assert(std::is_same_v<typename base_type::value_type, CharT>);

        basic_string_buffer_internal_overflow();

        constexpr basic_string_buffer_internal_overflow(
            overflow_provider_type&& overflow_provider) noexcept;

        constexpr basic_string_buffer_internal_overflow(
            basic_string_buffer_internal_overflow&& other) noexcept;

        basic_string_buffer_internal_overflow&
        operator=(basic_string_buffer_internal_overflow&& other) noexcept;

        basic_string_buffer_internal_overflow&
        operator=(basic_string_buffer_internal_overflow const& other);

        void
        swap(basic_string_buffer_internal_overflow& other) noexcept;

        using base_type::append;
        using base_type::assign;
        using base_type::clear;
        using base_type::push_back;

    private:
        using base_type::exceeds_inplace_vector;

        void
        append_to_overflow(span_type spn)
        {
            m_overflow_provider.append(spn);
        }

        void
        assign_to_overflow(span_type spn)
        {
            m_overflow_provider.assign(spn);
        }

        void
        clear_overflow()
        {
            m_overflow_provider.clear();
        }

        void
        push_back_to_overflow(base_type::value_type const& v)
        {
            m_overflow_provider.push_back(v);
        }

        void
        push_back_to_overflow(base_type::value_type&& v)
        {
            m_overflow_provider.push_back(std::move(v));
        }

        template <typename IteratorT>
        IteratorT
        append_overflow_to_iterator(IteratorT out)
        {
            return m_overflow_provider.copy_to(out);
        }

        std::size_t
        size_of_overflow() const noexcept
        {
            return m_overflow_provider.size();
        }

        std::optional<std::span<span_type>>
        get_overflow_spans(std::size_t starting_offset, std::span<span_type> spnspn) const noexcept
        {
            //
            // For the inline overflow string buffer, there is only a single overflow
            // span, so if the offset is nonzero, there will be no results.
            //

            if ((spnspn.size() == 0) || (starting_offset != 0))
                return std::nullopt;

            spnspn[0] = m_overflow_provider.span();

            return spnspn.subspan(0, 1);
        }

        template <typename Fn>
            requires(std::invocable<Fn, span_type>)
        void
        for_each_non_inplace_span(Fn&& fn) const
        {
            std::invoke(fn, m_overflow_provider.span());
        }

        overflow_provider_type m_overflow_provider;

        friend base_type;
    };

    template <typename CharT, std::size_t NInlineValueCount, typename OverflowProviderT>
        requires(std::is_nothrow_destructible_v<CharT>)
    basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>::
        basic_string_buffer_internal_overflow()
    {}

    template <typename CharT, std::size_t NInlineValueCount, typename OverflowProviderT>
        requires(std::is_nothrow_destructible_v<CharT>)
    constexpr basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>::
        basic_string_buffer_internal_overflow(overflow_provider_type&& overflow_provider) noexcept:
        m_overflow_provider(std::move(overflow_provider))
    {}

    template <typename CharT, std::size_t NInlineValueCount, typename OverflowProviderT>
        requires(std::is_nothrow_destructible_v<CharT>)
    constexpr basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>::
        basic_string_buffer_internal_overflow(
            basic_string_buffer_internal_overflow&& other) noexcept
    {
        using std::swap;

        base_type::swap(other);
        swap(m_overflow_provider, other.m_overflow_provider);
    }

    template <typename CharT, std::size_t NInlineValueCount, typename OverflowProviderT>
        requires(std::is_nothrow_destructible_v<CharT>)
    basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>&
    basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>::operator=(
        basic_string_buffer_internal_overflow&& other) noexcept
    {
        using std::swap;

        base_type::swap(other);
        swap(m_overflow_provider, other.m_overflow_provider);
        return *this;
    }

    template <typename CharT, std::size_t NInlineValueCount, typename OverflowProviderT>
        requires(std::is_nothrow_destructible_v<CharT>)
    basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>&
    basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>::operator=(
        basic_string_buffer_internal_overflow const& other)
    {
        assign(other);
        return *this;
    }

    template <typename CharT, std::size_t NInlineValueCount, typename OverflowProviderT>
        requires(std::is_nothrow_destructible_v<CharT>)
    void
    basic_string_buffer_internal_overflow<CharT, NInlineValueCount, OverflowProviderT>::swap(
        basic_string_buffer_internal_overflow& other) noexcept
    {
        using std::swap;

        base_type::swap(other);
        swap(m_overflow_provider, other.m_overflow_provider);
    }

} // namespace m

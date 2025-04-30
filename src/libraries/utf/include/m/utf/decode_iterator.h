// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

namespace m
{
    namespace utf
    {
        struct ucs_decoder_iterator_end_tag_t
        {
            explicit constexpr ucs_decoder_iterator_end_tag_t() = default;
        };

        static constexpr inline ucs_decoder_iterator_end_tag_t ucs_decoder_iterator_end_tag;

        template <typename CollectionT, typename SourceCharT>
        class ucs_decoder_iterator
        {
            using iter_type_t = decltype(std::ranges::cbegin(std::declval<CollectionT>()));
            using end_type_t  = decltype(std::ranges::cend(std::declval<CollectionT>()));

        public:
            using value_type      = char32_t;
            using difference_type = ptrdiff_t;

            constexpr ucs_decoder_iterator(CollectionT const& c, SourceCharT):
                m_next(std::ranges::cbegin(c)),
                m_end(std::ranges::cend(c)),
                m_ch{k_invalid_character}
            {
                try_advance();
            }

            constexpr ucs_decoder_iterator(ucs_decoder_iterator const& other):
                m_next(other.m_next), m_end(other.m_end), m_ch(other.m_ch)
            {}

            ~ucs_decoder_iterator() = default;

            void
            swap(ucs_decoder_iterator& other)
            {
                using std::swap;

                swap(m_next, other.m_next);
                swap(m_end, other.m_end);
                swap(m_ch, other.m_ch);
            }

            constexpr ucs_decoder_iterator&
            operator=(ucs_decoder_iterator const& other)
            {
                m_next = other.m_next;
                m_end  = other.m_end;
                m_ch   = other.m_ch;
                return *this;
            }

            friend constexpr bool
            operator==(ucs_decoder_iterator const& lhs, ucs_decoder_iterator_end_tag_t)
            {
                return (lhs.m_ch == k_invalid_character) && (lhs.m_next == lhs.m_end);
            }

            constexpr ucs_decoder_iterator&
            operator++()
            {
                try_advance();
                return *this;
            }

            constexpr ucs_decoder_iterator&
            operator++(int)
            {
                auto tmp = *this;
                ++*this;
                return tmp;
            }

            constexpr value_type
            operator*() const
            {
                return m_ch;
            }

        private:
            bool
            try_advance()
            {
                if (m_next == m_end)
                {
                    if (m_ch != k_invalid_character)
                        m_ch = k_invalid_character;

                    return false;
                }

                auto [next, ch] = decode_utf(SourceCharT{}, m_next, m_end);
                m_ch            = ch;
                m_next          = next;
                return true;
            }

            char32_t    m_ch;
            iter_type_t m_next;
            end_type_t  m_end;
        };

        template <typename CollectionT, typename SourceCharT>
        ucs_decoder_iterator(CollectionT, SourceCharT)
            -> ucs_decoder_iterator<CollectionT, SourceCharT>;

        template <typename CollectionT>
        struct ucs_decoder_selector;

        template <typename CollectionT>
        decltype(auto)
        decode_begin(CollectionT const& c)
        {
            auto t = ucs_decoder_selector<std::remove_cvref_t<CollectionT>>::decode_begin(c);
            static_assert(std::input_iterator<decltype(t)>);
            return t;
        }

        template <typename CollectionT>
        decltype(auto)
        decode_end(CollectionT const&)
        {
            return ucs_decoder_iterator_end_tag;
        }

        template <typename CharT>
        //            requires std::is_integral_v<CharT> && (sizeof(CharT) == 1)
        struct ucs_decoder_selector<std::basic_string_view<CharT>>
        {
            static decltype(auto)
            decode_begin(std::basic_string_view<CharT> const& v)
            {
                return ucs_decoder_iterator{v, CharT{}};
            }
        };
    } // namespace utf
} // namespace m

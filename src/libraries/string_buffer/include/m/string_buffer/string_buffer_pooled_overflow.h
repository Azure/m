// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <deque>
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
#include <m/pool/pool.h>
#include <m/utility/pointers.h>
#include <m/utility/smallest_size.h>

namespace m
{
    template <typename CharT,
              std::size_t NValuesInEachPoolItem,
              std::size_t NInlineItemsInPool,
              std::size_t NMaxExpansionSubpools,
              std::size_t NExpansionItemCount>
    using basic_string_buffer_pool_type = m::pool<m::inplace_vector<CharT, NValuesInEachPoolItem>,
                                                  NInlineItemsInPool,
                                                  NMaxExpansionSubpools,
                                                  NExpansionItemCount>;

    namespace string_buffer_impl
    {
        template <typename CharT,
                  std::size_t NValuesInEachPoolItem,
                  std::size_t NInlineItemsInPool,
                  std::size_t NMaxExpansionSubpools,
                  std::size_t NExpansionItemCount>
        struct pooled_overflow_provider
        {
            using value_type                                      = CharT;
            static inline constexpr auto values_in_each_pool_item = NValuesInEachPoolItem;
            static inline constexpr auto inline_items_in_pool     = NInlineItemsInPool;
            static inline constexpr auto max_expansion_subpools   = NMaxExpansionSubpools;
            static inline constexpr auto expansion_item_count     = NExpansionItemCount;

            using span_type         = std::span<value_type const>;
            using value_vector_type = inplace_vector<value_type, values_in_each_pool_item>;
            using pool_type         = basic_string_buffer_pool_type<value_type,
                                                                    values_in_each_pool_item,
                                                                    inline_items_in_pool,
                                                                    max_expansion_subpools,
                                                                    expansion_item_count>;

        private:
            struct entry
            {
                pool_type::unique_ptr_type m_ptr;
            };

        public:
            pooled_overflow_provider() = default;
            pooled_overflow_provider(std::shared_ptr<pool_type> const& pool): m_pool(pool) {}

            void
            assign(span_type spn)
            {
                auto       out_it  = m_deque.begin();
                auto const out_end = m_deque.end();

                while ((spn.size() != 0) && (out_it != out_end))
                    spn = fill_entry(*out_it++, spn);

                while (spn.size() != 0)
                {
                    auto& e = m_deque.emplace_back(entry{.m_ptr = m_pool->allocate()});
                    spn     = fill_entry(e, spn);
                }

                while (out_it != out_end)
                {
                    out_it++->m_ptr.reset();
                }
            }

            void
            append(span_type spn)
            {
                using std::swap;

                entry e;

                if (m_deque.empty())
                {
                    e.m_ptr = m_pool->allocate();
                }
                else
                {
                    // If the deque isn't empty, pull the last entry back into e
                    swap(m_deque.back(), e);
                    m_deque.pop_back();
                }

                // The invariant now is that e has a deque entry that
                // we try to fill. If it fills before spn is empty, we append
                // to the deque and iterate.
                while (spn.size() != 0)
                {
                    spn = append_entry(e, spn);
                    if (spn.size() != 0)
                    {
                        m_deque.emplace_back(std::move(e));
                        e.m_ptr = m_pool->allocate();
                    }
                }

                //
                if (e.m_ptr->size() != 0)
                    m_deque.emplace_back(std::move(e));
            }

            void
            clear()
            {
                m_deque.clear();
            }

            template <typename IteratorT>
            IteratorT
            copy_to(IteratorT outit)
            {
                for (auto const& e: m_deque)
                {
                    auto [in, out] = std::ranges::copy(*e.m_ptr, outit);
                    outit          = out;
                }

                return outit;
            }

            std::size_t
            size() const noexcept
            {
                std::size_t size{};

                for (auto const& e: m_deque)
                    size += e.m_ptr->size();

                return size;
            }

            void
            push_back(value_type const& v)
            {
                if (!m_deque.empty())
                {
                    auto& back = m_deque.back();
                    if (back.m_ptr->size() != back.m_ptr->capacity())
                    {
                        back.m_ptr->push_back(v);
                        return;
                    }
                }

                // We got here because either the deque was empty or the last entry was full. In
                // either case, allocate a new pool item, and add this to it. Neither will fail.
                entry e{.m_ptr = m_pool->allocate()};
                e.m_ptr->push_back(v);
                m_deque.push_back(std::move(e));
            }

            void
            push_back(value_type&& v)
            {
                if (!m_deque.empty())
                {
                    auto& back = m_deque.back();
                    if (back.m_ptr->size() != back.m_ptr->capacity())
                    {
                        back.m_ptr->push_back(std::move(v));
                        return;
                    }
                }

                // We got here because either the deque was empty or the last entry was full. In
                // either case, allocate a new pool item, and add this to it. Neither will fail.
                entry e{.m_ptr = m_pool->allocate()};
                e.m_ptr->push_back(std::move(v));
                m_deque.push_back(std::move(e));
            }

            std::optional<std::span<span_type>>
            get_overflow_spans(std::size_t          starting_offset,
                               std::span<span_type> spnspn) const noexcept
            {
                if (spnspn.size() == 0)
                    return std::nullopt;

                if (starting_offset >= m_deque.size())
                    return std::nullopt;

                auto        i_deque = starting_offset;
                std::size_t i{};

                while (i_deque < m_deque.size())
                {
                    auto const& e = m_deque[i_deque++];
                    spnspn[i++]   = std::span(e.m_ptr->begin(), e.m_ptr->end());

                    if (i == spnspn.size())
                        break;
                }

                return spnspn.subspan(0, i);
            }

        private:
            static span_type
            fill_entry(entry const& e, span_type spn)
            {
                if (spn.size() < e.m_ptr->capacity())
                {
                    e.m_ptr->resize(spn.size());
                    std::ranges::copy(spn, e.m_ptr->begin());
                    return span_type();
                }

                e.m_ptr->resize(e.m_ptr->capacity());
                std::ranges::copy(spn.subspan(0, e.m_ptr->capacity()), e.m_ptr->begin());

                return spn.subspan(e.m_ptr->capacity());
            }

            static span_type
            append_entry(entry const& e, span_type spn)
            {
                auto const e_size    = e.m_ptr->size();
                auto const charleft  = e.m_ptr->capacity() - e_size;
                auto const charcount = std::min(charleft, spn.size());

                e.m_ptr->resize(e_size + charcount);
                std::ranges::copy(spn.subspan(0, charcount), e.m_ptr->begin() + e_size);

                return spn.subspan(charcount);
            }

            std::shared_ptr<pool_type> m_pool;
            std::deque<entry>          m_deque;
        };
    } // namespace string_buffer_impl

    template <typename CharT,
              std::size_t NInlineValueCount,
              std::size_t NValuesInEachPoolItem,
              std::size_t NInlineItemsInPool,
              std::size_t NMaxExpansionSubpools,
              std::size_t NExpansionItemCount>
        requires(std::is_nothrow_destructible_v<CharT>)
    class basic_string_buffer_pooled_overflow :
        public basic_string_buffer_base<CharT,
                                        NInlineValueCount,
                                        basic_string_buffer_pooled_overflow<CharT,
                                                                            NInlineValueCount,
                                                                            NValuesInEachPoolItem,
                                                                            NInlineItemsInPool,
                                                                            NMaxExpansionSubpools,
                                                                            NExpansionItemCount>>
    {
    public:
        using value_type                                      = CharT;
        static inline constexpr auto inline_value_count       = NInlineValueCount;
        static inline constexpr auto values_in_each_pool_item = NValuesInEachPoolItem;
        static inline constexpr auto inline_items_in_pool     = NInlineItemsInPool;
        static inline constexpr auto max_expansion_subpools   = NMaxExpansionSubpools;
        static inline constexpr auto expansion_item_count     = NExpansionItemCount;

        using base_type =
            basic_string_buffer_base<value_type,
                                     inline_value_count,
                                     basic_string_buffer_pooled_overflow<value_type,
                                                                         inline_value_count,
                                                                         values_in_each_pool_item,
                                                                         inline_items_in_pool,
                                                                         max_expansion_subpools,
                                                                         expansion_item_count>>;

        using this_type = basic_string_buffer_pooled_overflow<value_type,
                                                              inline_value_count,
                                                              values_in_each_pool_item,
                                                              inline_items_in_pool,
                                                              max_expansion_subpools,
                                                              expansion_item_count>;

        using pool_type = basic_string_buffer_pool_type<value_type,
                                                        values_in_each_pool_item,
                                                        inline_items_in_pool,
                                                        max_expansion_subpools,
                                                        expansion_item_count>;

        using typename base_type::char_traits;
        using typename base_type::czstring_type;
        using typename base_type::span_type;
        using typename base_type::string_type;
        using typename base_type::string_view_type;

        basic_string_buffer_pooled_overflow() = delete;

        basic_string_buffer_pooled_overflow(std::shared_ptr<pool_type> const& pt);

        constexpr basic_string_buffer_pooled_overflow(
            basic_string_buffer_pooled_overflow&& other) noexcept;

        basic_string_buffer_pooled_overflow(basic_string_buffer_pooled_overflow const& other);

        basic_string_buffer_pooled_overflow&
        operator=(basic_string_buffer_pooled_overflow&& other) noexcept;

        basic_string_buffer_pooled_overflow&
        operator=(basic_string_buffer_pooled_overflow const& other);

        void
        swap(basic_string_buffer_pooled_overflow& other) noexcept;

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

        string_buffer_impl::pooled_overflow_provider<value_type,
                                                     values_in_each_pool_item,
                                                     inline_items_in_pool,
                                                     max_expansion_subpools,
                                                     expansion_item_count>
            m_overflow_provider;

        friend base_type;
    };

    template <typename CharT,
              std::size_t NInlineValueCount,
              std::size_t NValuesInEachPoolItem,
              std::size_t NInlineItemsInPool,
              std::size_t NMaxExpansionSubpools,
              std::size_t NExpansionItemCount>
        requires(std::is_nothrow_destructible_v<CharT>)
    basic_string_buffer_pooled_overflow<CharT,
                                        NInlineValueCount,
                                        NValuesInEachPoolItem,
                                        NInlineItemsInPool,
                                        NMaxExpansionSubpools,
                                        NExpansionItemCount>::
        basic_string_buffer_pooled_overflow(std::shared_ptr<pool_type> const& pt):
        m_overflow_provider(pt)
    {
    }

    template <typename CharT,
              std::size_t NInlineValueCount,
              std::size_t NValuesInEachPoolItem,
              std::size_t NInlineItemsInPool,
              std::size_t NMaxExpansionSubpools,
              std::size_t NExpansionItemCount>
        requires(std::is_nothrow_destructible_v<CharT>)
    constexpr basic_string_buffer_pooled_overflow<CharT,
                                                  NInlineValueCount,
                                                  NValuesInEachPoolItem,
                                                  NInlineItemsInPool,
                                                  NMaxExpansionSubpools,
                                                  NExpansionItemCount>::
        basic_string_buffer_pooled_overflow(basic_string_buffer_pooled_overflow&& other) noexcept
    {
        using std::swap;

        base_type::swap(other);
        swap(m_overflow_provider, other.m_overflow_provider);
    }

    template <typename CharT,
              std::size_t NInlineValueCount,
              std::size_t NValuesInEachPoolItem,
              std::size_t NInlineItemsInPool,
              std::size_t NMaxExpansionSubpools,
              std::size_t NExpansionItemCount>
        requires(std::is_nothrow_destructible_v<CharT>)
    basic_string_buffer_pooled_overflow<CharT,
                                        NInlineValueCount,
                                        NValuesInEachPoolItem,
                                        NInlineItemsInPool,
                                        NMaxExpansionSubpools,
                                        NExpansionItemCount>::
        basic_string_buffer_pooled_overflow(basic_string_buffer_pooled_overflow const& other):
        m_overflow_provider(other.m_overflow_provider)
    {
        assign(other);
    }

    template <typename CharT,
              std::size_t NInlineValueCount,
              std::size_t NValuesInEachPoolItem,
              std::size_t NInlineItemsInPool,
              std::size_t NMaxExpansionSubpools,
              std::size_t NExpansionItemCount>
        requires(std::is_nothrow_destructible_v<CharT>)
    basic_string_buffer_pooled_overflow<CharT,
                                        NInlineValueCount,
                                        NValuesInEachPoolItem,
                                        NInlineItemsInPool,
                                        NMaxExpansionSubpools,
                                        NExpansionItemCount>&
    basic_string_buffer_pooled_overflow<
        CharT,
        NInlineValueCount,
        NValuesInEachPoolItem,
        NInlineItemsInPool,
        NMaxExpansionSubpools,
        NExpansionItemCount>::operator=(basic_string_buffer_pooled_overflow&& other) noexcept
    {
        using std::swap;

        base_type::swap(other);
        swap(m_overflow_provider, other.m_overflow_provider);
        return *this;
    }

    template <typename CharT,
              std::size_t NInlineValueCount,
              std::size_t NValuesInEachPoolItem,
              std::size_t NInlineItemsInPool,
              std::size_t NMaxExpansionSubpools,
              std::size_t NExpansionItemCount>
        requires(std::is_nothrow_destructible_v<CharT>)
    basic_string_buffer_pooled_overflow<CharT,
                                        NInlineValueCount,
                                        NValuesInEachPoolItem,
                                        NInlineItemsInPool,
                                        NMaxExpansionSubpools,
                                        NExpansionItemCount>&
    basic_string_buffer_pooled_overflow<
        CharT,
        NInlineValueCount,
        NValuesInEachPoolItem,
        NInlineItemsInPool,
        NMaxExpansionSubpools,
        NExpansionItemCount>::operator=(basic_string_buffer_pooled_overflow const& other)
    {
        assign(other);
        return *this;
    }

    template <typename CharT,
              std::size_t NInlineValueCount,
              std::size_t NValuesInEachPoolItem,
              std::size_t NInlineItemsInPool,
              std::size_t NMaxExpansionSubpools,
              std::size_t NExpansionItemCount>
        requires(std::is_nothrow_destructible_v<CharT>)
    void
    basic_string_buffer_pooled_overflow<
        CharT,
        NInlineValueCount,
        NValuesInEachPoolItem,
        NInlineItemsInPool,
        NMaxExpansionSubpools,
        NExpansionItemCount>::swap(basic_string_buffer_pooled_overflow& other) noexcept
    {
        using std::swap;

        base_type::swap(other);
        swap(m_overflow_provider, other.m_overflow_provider);
    }
} // namespace m

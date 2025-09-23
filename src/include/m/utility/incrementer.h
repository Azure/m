// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <atomic>
#include <concepts>
#include <stdexcept>
#include <type_traits>

namespace m
{
    namespace incrementer_impl
    {
        template <typename T>
            requires(std::integral<T>)
        class naive_incrementer
        {
        public:
            naive_incrementer(T& rt): m_rt(rt) { m_rt++; }
            ~naive_incrementer() { m_rt--; }

        private:
            T& m_rt;
        };

        template <typename T>
            requires(std::integral<T>)
        class integral_incrementer
        {
        public:
            integral_incrementer(T& rt): m_rt(rt) { m_rt++; }
            ~integral_incrementer() { m_rt--; }

        private:
            T& m_rt;
        };

        template <typename T>
            requires(std::integral<T>)
        class atomic_incrementer
        {
        public:
            atomic_incrementer(std::atomic<T>&   rt,
                               std::memory_order mo = std::memory_order_seq_cst):
                m_rt(rt), m_mo(mo)
            {
                m_rt.fetch_add(1, m_mo);
            }
            ~atomic_incrementer() { m_rt.fetch_sub(1, m_mo); }

        private:
            std::atomic<T>&   m_rt;
            std::memory_order m_mo;
        };

        template <typename T>
        using incrementer_base_chooser =
            std::conditional_t<std::integral<T>, integral_incrementer<T>, naive_incrementer<T>>;
    } // namespace incrementer_impl

    template <typename T>
    struct incrementer : incrementer_impl::incrementer_base_chooser<T>
    {
        using base_type = incrementer_impl::incrementer_base_chooser<T>;

        using base_type::base_type;
    };

    template <typename T>
    incrementer(T& rt) -> incrementer<T>;
} // namespace m

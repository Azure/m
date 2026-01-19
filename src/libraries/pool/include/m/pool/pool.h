// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include <m/bitset/bitset.h>
#include <m/puddle/puddle.h>
#include <m/utility/incrementer.h>
#include <m/utility/locked.h>
#include <m/utility/pointers.h>

namespace m
{
    /// <summary>
    /// The `pool` class defines a pool of allocated (but not constructed)
    /// objects from which the caller may allocate and release.
    ///
    /// It may or may not be faster than interacting with the heap,
    /// the intention is more around cache locality and perhaps constraining
    /// the growth of the number of objects.
    ///
    /// If `NExtensionLimit` or `NExtensionSize` are zero, the pool cannot
    /// grow past its initial size, and in any case, it will not grow past
    /// `NExtensionLimit` additional allocation groups.
    ///
    /// In a very basic way, consider the pool to be an array of
    /// `NInlineItemCount` `T`s, followed by up to `NExtensionLimit` additional
    /// arrays of `NExtensionSize` `T`s.
    ///
    /// In the initial version, no effort is made to make the allocation
    /// lock-free or to reclaim unallocated groups. Clearly both can be
    /// done reasonably trivially but the main point here is to enable some
    /// generic measure of constrained allocation.
    ///
    /// The initial consumer of this capability is for strings for the
    /// tracing library. Before this, each message has a fixed sized buffer
    /// of 4096 characters. This is far larger than needed for most messages
    /// and far shorter than long messages need. Rather than trying to enable
    /// some complex heap-based growth heuristic for the string which can
    /// fail (e.g. format the message, if that isn't big enough, allocate
    /// a new buffer 2x as big, repeat), or count the size first which would
    /// possibly require rewriting consumers of the logging interfaces
    /// or other untenable solutions, instead we will enable an output
    /// iterator that will grab puddles from a pool as needed, up to some
    /// limit. If the pool becomes empty, it will wait for the queue to
    /// drain and the pool to refill.
    ///
    /// Since the array/bitmap discipline has moved out to the `puddle` object,
    /// the `pool` object only maintains the synchronization, and the
    /// knowledge of the initial puddle of instances vs. the expansion
    /// puddles.
    ///
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <typeparam name="NInlineItemCount"></typeparam>
    /// <typeparam name="NMaxExpansionSubpools"></typeparam>
    /// <typeparam name="NExpansionItemCount"></typeparam>

    template <typename T,
              std::size_t NInlineItemCount,
              std::size_t NMaxExpansionSubpools = 0,
              std::size_t NExpansionItemCount   = (std::max)((1ull << 20) / sizeof(T), 512ull)>
        requires(NInlineItemCount != 0)
    class pool :
        public std::enable_shared_from_this<
            pool<T, NInlineItemCount, NMaxExpansionSubpools, NExpansionItemCount>>
    {
        using this_type = pool<T, NInlineItemCount, NMaxExpansionSubpools, NExpansionItemCount>;

    public:
        using value_type = T;

        static inline constexpr std::size_t inline_item_count     = NInlineItemCount;
        static inline constexpr std::size_t max_expansion_puddles = NMaxExpansionSubpools;
        static inline constexpr std::size_t expansion_item_count  = NExpansionItemCount;

        using puddle_type = puddle<value_type, inline_item_count>;
        using slot_type   = typename puddle_type::count_type;

        pool()
        {
            m_puddles[0]   = &m_initial_puddle;
            m_puddle_count = 1;
            m_puddle_span  = std::span(m_puddles.data(), m_puddle_count);
        }

        pool(pool const& other) = delete;
        pool(pool&& other)      = delete;

        virtual ~pool()
        {
            // For the pool to be deallocated, all the references
            // from the shared_ptr<> instances need to be
            // gone, which means that all of the unique_ptr<>s
            // are presumably gone. If all the unique_ptr<>s
            // on the pool entries are gone, how are there any
            // allocated pool entries??
            M_INTERNAL_ERROR_CHECK(m_active_allocations == 0);
        }

        pool&
        operator=(pool const&) = delete;

        pool&
        operator=(pool&&) = delete;

        struct deleter
        {
            deleter() = default;

            deleter(std::shared_ptr<pool> const& sp,
                    puddle_type*                 unique_puddle_ptr,
                    slot_type                    slot):
                m_sp(sp), m_puddle(unique_puddle_ptr), m_slot(slot)
            {}

            deleter(deleter&& other)
            {
                using std::swap;

                swap(m_sp, other.m_sp);
                swap(m_puddle, other.m_puddle);
                swap(m_slot, other.m_slot);
            }
            ~deleter() = default;

            deleter&
            operator=(deleter&& other)
            {
                using std::swap;

                swap(m_sp, other.m_sp);
                swap(m_puddle, other.m_puddle);
                swap(m_slot, other.m_slot);

                return *this;
            }

            void
            operator()(T* ptr)
            {
                m_sp->deallocate(ptr, m_puddle, m_slot);
            }

        private:
            std::shared_ptr<this_type> m_sp;
            puddle_type*               m_puddle{};
            slot_type                  m_slot{};
        };

        using unique_ptr_type = std::unique_ptr<T, deleter>;

        struct internal_allocation_result
        {
            T*           m_ptr;
            puddle_type* m_puddle;
            slot_type    m_slot;

            internal_allocation_result(puddle_type* ptr, puddle_type::allocation_result const& ar):
                m_ptr(ar.m_ptr), m_puddle(ptr), m_slot(ar.m_slot)
            {}

            unique_ptr_type
            to_unique_ptr(std::shared_ptr<pool> const& sp)
            {
                return unique_ptr_type(m_ptr, deleter(sp, m_puddle, m_slot));
            }
        };

        unique_ptr_type
        allocate()
        {
            auto l = std::unique_lock(m_mutex);

            // lambda that returns true/false based on whether the free count is zero
            auto wait_predicate = [this] { return m_active_allocations != m_pool_size; };

            // Lambda that takes a unique_lock<mutex> and waits on this->m_cv for
            // wait_predicate to become true
            auto wait_lambda = [&](m::locked_t) { m_cv.wait(l, wait_predicate); };

            auto rv = internal_allocate(m::locked, wait_lambda);
            static_assert(std::is_same_v<decltype(rv), unique_ptr_type>);
            m_active_allocations++;
            return rv;
        }

        // Who really wants to try to allocate for a period of time and give up?
        //
        // However, sitting in a loop waiting indefinitely for memory to become
        // available also seems like a poor experience so the ..._for() and
        // ..._until() variants are provided for callers.
        template <typename Rep, typename Period>
        std::optional<unique_ptr_type>
        allocate_for(std::chrono::duration<Rep, Period> const& d)
        {
            auto l = std::unique_lock(m_mutex);

            // lambda that returns true/false based on whether the free count is zero
            auto wait_predicate = [this] { return m_active_allocations != m_pool_size; };

            auto const wait_lambda = [&](m::locked_t) -> bool {
                return m_cv.wait_for(l, d, wait_predicate);
            };

            auto orv = internal_allocate(m::locked, wait_lambda);

            if (orv.has_value())
                m_active_allocations++;

            return orv;
        }

        // Who really wants to try to allocate for a period of time and give up?
        //
        // However, sitting in a loop waiting indefinitely for memory to become
        // available also seems like a poor experience so the ..._for() and
        // ..._until() variants are provided for callers.
        template <typename Clock, typename Duration>
        std::optional<unique_ptr_type>
        allocate_for(std::chrono::time_point<Clock, Duration> const& tp)
        {
            auto l = std::unique_lock(m_mutex);

            // lambda that returns true/false based on whether the free count is zero
            auto wait_predicate = [this] { return m_active_allocations != m_pool_size; };

            auto const wait_lambda = [&](m::locked_t) -> bool {
                return m_cv.wait_until(l, tp, wait_predicate);
            };

            auto orv = internal_allocate(m::locked, wait_lambda);

            if (orv.has_value())
                m_active_allocations++;

            return orv;
        }

    private:
        void
        deallocate(T* ptr, puddle_type* unique_puddle_ptr, slot_type slot)
        {
            std::destroy_n(ptr, 1);

            {
                auto l = std::unique_lock(m_mutex);
                // Use `release` on the puddle since we already destroyed
                // the object outside of holding the mutex
                unique_puddle_ptr->release(slot);
                m_active_allocations--;
            }

            m_cv.notify_one();
        }

        // If the WaiterT returns void, then internal_allocate waits until a
        // value is available, so the return type is `unique_ptr_type`.
        template <typename WaiterT>
            requires(std::invocable<WaiterT, m::locked_t> &&
                     std::is_void_v<std::invoke_result_t<WaiterT, m::locked_t>>)
        unique_ptr_type
        internal_allocate(m::locked_t, WaiterT&& waiter)
        {
            for (;;)
            {
                if (auto oiar = try_internal_allocate(m::locked); oiar.has_value())
                    return oiar.value().to_unique_ptr(this->shared_from_this());

                m::incrementer waiter_increment(m_waiter_count);

                // Let the caller wait in whatever way they wanted
                std::invoke(std::move(waiter), m::locked);
            }
        }

        // If the WaiterT returns bool, then internal_allocate waits until a
        // value is available or the invocable return false, so the return type is
        // `std::optional<unique_ptr_type>`.
        template <typename WaiterT>
            requires(std::invocable<WaiterT, m::locked_t> &&
                     std::is_same_v<bool, std::invoke_result_t<WaiterT, m::locked_t>>)
        std::optional<unique_ptr_type>
        internal_allocate(m::locked_t, WaiterT&& waiter)
        {
            for (;;)
            {
                if (auto oiar = try_internal_allocate(m::locked); oiar.has_value())
                    return oiar.value().to_unique_ptr(this->shared_from_this());

                if (auto oiar = try_allocate_from_new_puddle(m::locked); oiar.has_value())
                    return oiar.value().to_unique_ptr(this->shared_from_this());

                m::incrementer waiter_increment(m_waiter_count);

                // Let the caller wait in whatever way they wanted
                if (!std::invoke(std::move(waiter), m::locked))
                    return std::nullopt;
            }
        }

        std::optional<internal_allocation_result>
        try_allocate_from_new_puddle(m::locked_t)
        {
            if (m_expansion_puddle_count == max_expansion_puddles)
                return std::nullopt;

            M_INTERNAL_ERROR_CHECK(m_expansion_puddles[m_expansion_puddle_count].get() == nullptr);
            M_INTERNAL_ERROR_CHECK(m_puddle_count == m_expansion_puddle_count + 1);

            m_expansion_puddles[m_expansion_puddle_count] = std::make_unique<puddle_type>();
            m_puddles[m_puddle_count] = m_expansion_puddles[m_expansion_puddle_count].get();

            auto const new_puddle = m_puddles[m_puddle_count]; // remember this for later

            m_expansion_puddle_count++;
            m_puddle_count++;

            m_pool_size += expansion_item_count;

            m_puddle_span = std::span(m_puddles.data(), m_puddle_count);

            auto ar = new_puddle->try_allocate();
            // If a new pool can't allocate a slot, we have a bug.
            M_INTERNAL_ERROR_CHECK(ar.has_value());
            return internal_allocation_result(new_puddle, ar.value());
        }

        std::optional<internal_allocation_result>
        try_internal_allocate(m::locked_t)
        {
            for (auto const& e: m_puddle_span)
                if (auto result = e->try_allocate(); result.has_value())
                    return internal_allocation_result(e, result.value());

            return std::nullopt;
        }

        using unique_puddle_ptr = std::unique_ptr<puddle_type>;

        std::mutex                                           m_mutex;
        std::condition_variable                              m_cv;
        std::size_t                                          m_waiter_count{};
        std::size_t                                          m_pool_size{inline_item_count};
        std::size_t                                          m_active_allocations{};
        std::size_t                                          m_puddle_count{};
        std::size_t                                          m_expansion_puddle_count{};
        std::span<puddle_type*>                              m_puddle_span;
        std::array<puddle_type*, max_expansion_puddles + 1>  m_puddles;
        std::array<unique_puddle_ptr, max_expansion_puddles> m_expansion_puddles;
        puddle_type                                          m_initial_puddle;

        friend struct deleter;
    };

    template <typename T,
              std::size_t NInlineItemCount,
              std::size_t NMaxExpansionSubpools = 0,
              std::size_t NExpansionItemCount   = (std::max)((1ull << 20) / sizeof(T), 512ull)>
        requires(NInlineItemCount != 0)
    using pool_size = std::integral_constant<
        std::size_t,
        sizeof(m::pool<T, NInlineItemCount, NMaxExpansionSubpools, NExpansionItemCount>)>;

    template <typename T,
              std::size_t NInlineItemCount,
              std::size_t NMaxExpansionSubpools = 0,
              std::size_t NExpansionItemCount   = (std::max)((1ull << 20) / sizeof(T), 512ull)>
        requires(NInlineItemCount != 0)
    constexpr std::size_t pool_size_v =
        pool_size<T, NInlineItemCount, NMaxExpansionSubpools, NExpansionItemCount>::value;

} // namespace m

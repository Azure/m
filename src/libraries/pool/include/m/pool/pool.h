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
    /// `NInitialSize` `T`s, followed by up to `NExtensionLimit` additional
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
    /// iterator that will grab subpools from a pool as needed, up to some
    /// limit. If the pool becomes empty, it will wait for the queue to
    /// drain and the pool to refill.
    /// </summary>
    /// <typeparam name="T"></typeparam>
    /// <typeparam name="NInitialSize"></typeparam>
    /// <typeparam name="NExpansionLimit"></typeparam>
    /// <typeparam name="NExpansionSize"></typeparam>

    template <typename T,
              std::size_t NInitialSize,
              std::size_t NExpansionLimit = 0,
              std::size_t NExpansionSize  = (std::max)((1ull << 10) / sizeof(T), 512ull)>
        requires(NInitialSize != 0)
    class pool :
        public std::enable_shared_from_this<pool<T, NInitialSize, NExpansionLimit, NExpansionSize>>
    {
        using this_type = pool<T, NInitialSize, NExpansionLimit, NExpansionSize>;

        static inline constexpr std::size_t initial_size    = NInitialSize;
        static inline constexpr std::size_t expansion_limit = NExpansionLimit;
        static inline constexpr std::size_t expansion_size  = NExpansionSize;

        struct subpool_base;

    public:
        pool()
        {
            m_subpools[0]   = &m_initial_subpool;
            m_subpool_count = 1;
            m_subpool_span  = std::span(m_subpools.data(), m_subpool_count);
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

        struct pool_alloc_deleter
        {
            pool_alloc_deleter() = default;

            pool_alloc_deleter(std::shared_ptr<pool> const& sp,
                               subpool_base*                ptr,
                               std::size_t                  slot):
                m_sp(sp), m_subpool_base(ptr), m_slot(slot)
            {}

            pool_alloc_deleter(pool_alloc_deleter&& other)
            {
                using std::swap;

                swap(m_sp, other.m_sp);
                swap(m_subpool_base, other.m_subpool_base);
                swap(m_slot, other.m_slot);
            }
            ~pool_alloc_deleter() = default;

            pool_alloc_deleter&
            operator=(pool_alloc_deleter&& other)
            {
                using std::swap;

                swap(m_sp, other.m_sp);
                swap(m_subpool_base, other.m_subpool_base);
                swap(m_slot, other.m_slot);

                return *this;
            }

            void
            operator()(T* ptr)
            {
                m_sp->deallocate(ptr, m_subpool_base, m_slot);
            }

        private:
            std::shared_ptr<this_type> m_sp;
            subpool_base*              m_subpool_base{};
            std::size_t                m_slot{};
        };

        using unique_ptr_type = std::unique_ptr<T, pool_alloc_deleter>;

        struct internal_allocation_result
        {
            T*            m_ptr;
            subpool_base* m_subpool_base;
            std::size_t   m_slot;

            unique_ptr_type
            to_unique_ptr(std::shared_ptr<pool> const& sp)
            {
                return unique_ptr_type(m_ptr, pool_alloc_deleter(sp, m_subpool_base, m_slot));
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
        allocate_for(std::chrono::duration<Rep, Period> d)
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
        allocate_for(std::chrono::time_point<Clock, Duration> tp)
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
        struct subpool_base
        {
            virtual ~subpool_base() = default;

            virtual std::optional<internal_allocation_result>
            try_allocate(m::locked_t) = 0;

            virtual void
            deallocate(m::locked_t, std::size_t slot) = 0;

            std::size_t m_free{};
        };

        template <std::size_t N, typename enable = void>
        struct subpool;

        template <std::size_t N>
        struct subpool<N, std::enable_if_t<N != 0>> : public subpool_base
        {
            using subpool_base::m_free;

            using this_type         = subpool<N>;
            using expansion_subpool = subpool<pool::expansion_size>;

            subpool() { m_free = m_in_use_map.size(); }
            subpool(subpool const&) = delete;
            subpool(subpool&&)      = delete;
            ~subpool()              = default;

            subpool&
            operator=(subpool const&) = delete;

            subpool&
            operator=(subpool&&) = delete;

            std::optional<internal_allocation_result>
            try_allocate(m::locked_t) override
            {
                if (m_free == 0)
                    return std::nullopt;

                bitset_bit_allocator<N> ba(m_in_use_map);
                // auto r1 = m_in_use_map.find_first_clear_and_set();

                if (!ba.has_value())
                    return std::nullopt;

                auto const index        = ba.bit();
                auto const ptr          = reinterpret_cast<T*>(&m_data[sizeof(T) * index]);
                auto const return_value = internal_allocation_result{
                    .m_ptr = ::new (ptr) T, .m_subpool_base = this, .m_slot = index};
                m_free--;
                ba.release();

                return return_value;
            }

            void
            deallocate(m::locked_t, std::size_t slot) override
            {
                m_in_use_map.clear(slot);
                m_free++;
            }

            m::bitset<N> m_in_use_map;
            alignas(T) std::array<std::byte, sizeof(T) * N> m_data;
        };

        template <std::size_t N>
        struct subpool<N, std::enable_if_t<N == 0>> : public subpool_base
        {
            using subpool_base::m_free;

            using this_type = subpool<N>;

            subpool()               = default;
            subpool(subpool const&) = delete;
            subpool(subpool&&)      = delete;
            ~subpool()              = default;

            subpool&
            operator=(subpool const&) = delete;

            subpool&
            operator=(subpool&&) = delete;

            std::optional<internal_allocation_result>
            try_allocate(m::locked_t) override
            {
                // zero size, no need to do anything
                return std::nullopt;
            }

            void
            deallocate(m::locked_t, std::size_t) override
            {
                M_NOT_IMPLEMENTED(
                    "No allocations from this pool; should be no deallocations to it");
            }
        };

        void
        deallocate(T* ptr, subpool_base* subpool_base_ptr, std::size_t slot)
        {
            // Destroy the object before taking the mutex, there is no need for
            // synchronization around that.
            std::destroy_n(ptr, 1);

            auto l = std::unique_lock(m_mutex);
            subpool_base_ptr->deallocate(m::locked, slot);
            m_active_allocations--;
            l.unlock();
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

                if (auto oiar = try_allocate_from_new_subpool(m::locked); oiar.has_value())
                    return oiar.value().to_unique_ptr(this->shared_from_this());

                m::incrementer waiter_increment(m_waiter_count);

                // Let the caller wait in whatever way they wanted
                if (!std::invoke(std::move(waiter), m::locked))
                    return std::nullopt;
            }
        }

        std::optional<internal_allocation_result>
        try_allocate_from_new_subpool(m::locked_t)
        {
            if (m_expansion_subpool_count == expansion_limit)
                return std::nullopt;

            M_INTERNAL_ERROR_CHECK(m_expansion_subpools[m_expansion_subpool_count].get() ==
                                   nullptr);
            M_INTERNAL_ERROR_CHECK(m_subpool_count == m_expansion_subpool_count + 1);

            m_expansion_subpools[m_expansion_subpool_count] =
                std::make_unique<subpool<expansion_size>>();
            m_subpools[m_subpool_count] = m_expansion_subpools[m_expansion_subpool_count].get();

            auto const new_pool = m_subpools[m_subpool_count]; // remember this for later

            m_expansion_subpool_count++;
            m_subpool_count++;

            m_pool_size += expansion_size;

            m_subpool_span = std::span(m_subpools.data(), m_subpool_count);

            auto oiar = new_pool->try_allocate(m::locked);
            // If a new pool can't allocate a slot, we have a bug.
            M_INTERNAL_ERROR_CHECK(oiar.has_value());
            return oiar;
        }

        std::optional<internal_allocation_result>
        try_internal_allocate(m::locked_t)
        {
            for (auto const& e: m_subpool_span)
                if (auto result = e->try_allocate(m::locked); result.has_value())
                    return result;

            return std::nullopt;
        }

        std::mutex                                     m_mutex;
        std::condition_variable                        m_cv;
        std::size_t                                    m_waiter_count{};
        std::size_t                                    m_pool_size{initial_size};
        std::size_t                                    m_active_allocations{};
        std::size_t                                    m_subpool_count{};
        std::size_t                                    m_expansion_subpool_count{};
        std::span<subpool_base*>                       m_subpool_span;
        std::array<subpool_base*, expansion_limit + 1> m_subpools;
        subpool<initial_size>                          m_initial_subpool;
        std::array<std::unique_ptr<subpool<expansion_size>>, expansion_limit> m_expansion_subpools;

        friend struct pool_alloc_deleter;
    };
} // namespace m

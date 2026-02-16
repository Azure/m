// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <mutex>
#include <shared_mutex>

namespace m
{
    //
    // std::unique_lock<> may hold a lock, release it and reacquire it.
    //
    // This is the same kind of operation that deserves a RAII wrapper
    // as any.
    //
    // It's tempting to try to make unique_unlock<> have the same API as
    // unique_lock<> but things get very confusing very quickly. This
    // iteration of the implementation (there have been several in the past)
    // only has the constructor and destructor, albeit it does have the
    // ability to disarm early. It's not 100% clear what to call that
    // behavior.
    //

    template <typename Lockable, typename Enable>
    class unique_unlock;

    template <typename MutexT>
    class unique_unlock<std::unique_lock<MutexT>,
                        std::enable_if_t<std::same_as<MutexT, std::mutex> ||
                                         std::same_as<MutexT, std::shared_mutex>>>
    {
    public:
        using mutex_type       = MutexT;
        using unique_lock_type = std::unique_lock<mutex_type>;

        unique_unlock() = delete;

        unique_unlock(unique_lock_type& lock): m_lock(&lock), m_armed(false)
        {
            if (!m_lock->owns_lock())
                throw std::runtime_error("lock must be owned");

            m_lock->unlock();
            m_armed = true;
        }

        unique_unlock(unique_unlock const&) = delete;

        constexpr unique_unlock(unique_unlock&& other) noexcept:
            m_lock(other.m_lock), m_armed(other.m_armed)
        {
            other.m_lock  = nullptr;
            other.m_armed = false;
        }

        ~unique_unlock() { reset(); }

        void
        operator=(unique_unlock const&) = delete;

        unique_unlock&
        operator=(unique_unlock&& other) noexcept
        {
            using std::swap;
            swap(m_lock, other.m_lock);
            swap(m_armed, other.m_armed);
            return *this;
        }

        void
        swap(unique_unlock& other) noexcept
        {
            using std::swap;
            swap(m_lock, other.m_lock);
            swap(m_armed, other.m_armed);
        }

        constexpr auto
        mutex() const noexcept
        {
            return m_lock->mutex();
        }

        constexpr auto
        underlying_lock() const noexcept
        {
            return m_lock;
        }

        constexpr
        operator bool() const noexcept
        {
            return unlocked_lock();
        }

        constexpr bool
        release() noexcept
        {
            return std::exchange(m_armed, false);
        }

        void
        reset() noexcept
        {
            if (std::exchange(m_armed, false))
            {
                m_lock->lock();
            }
        }

        /// <summary>
        /// This is the anti-`owns_lock()` observer.
        ///
        /// Instead of reporting whether the underlying lockable is locked,
        /// it reports on whether the underlying lockable is unlocked.
        /// </summary>
        /// <returns></returns>
        constexpr bool
        unlocked_lock() const noexcept
        {
            return m_armed;
        }

    private:
        unique_lock_type* m_lock;
        bool              m_armed; // Called "armed" since true => destructor / reset need to do something
    };

    template <typename MutexT>
    unique_unlock(std::unique_lock<MutexT>& x)
        -> unique_unlock<std::unique_lock<MutexT>,
                         std::enable_if_t<std::same_as<MutexT, std::mutex> ||
                                          std::same_as<MutexT, std::shared_mutex>>>;

} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <atomic>
#include <compare>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <m/utility/pointers.h>

namespace m
{
#ifdef M_HAS_CXX23
    template <typename T, typename Fn>
        requires(std::is_pointer_v<T>)
    T
    racy_initialize(std::atomic<T>& x, Fn&& fn)
    {
        T retval = x.load(std::memory_order_acquire);

        if (retval)
            return retval;

        auto newval =
            std::unique_ptr<std::remove_pointer_t<T>>(std::invoke_r<T, Fn>(std::forward<Fn>(fn)));

        while (retval == nullptr)
        {
            if (x.compare_exchange_strong(retval, newval.get(), std::memory_order_acq_rel))
            {
                newval.release();
            }
        }

        return x.load(std::memory_order_acquire);
    }
#endif // M_HAS_CXX23

    template <typename T,
              T f() = []() -> T {
                  using X = std::remove_pointer_t<T>;
                  return new X;
              },
              void g(T) = [](T ptr) { delete ptr; }>
        requires(std::is_pointer_v<T>)
    class atomic_pointer_with_initializer
    {
    public:
        atomic_pointer_with_initializer() {}
        atomic_pointer_with_initializer(atomic_pointer_with_initializer const& other) = delete;
        atomic_pointer_with_initializer(atomic_pointer_with_initializer&& other)      = delete;
        ~atomic_pointer_with_initializer()
        {
            T ptr = m_ptr.exchange(nullptr, std::memory_order_acq_rel);
            if (ptr != nullptr)
            {
                g(ptr);
            }
        }

        void
        operator=(atomic_pointer_with_initializer const&) = delete;

        void
        operator=(atomic_pointer_with_initializer&& other) = delete;

        operator T() { return racy_initialize(m_ptr, f); }

        operator not_null<T>() { return racy_initialize(m_ptr, f); }

        T
        operator->()
        {
            return racy_initialize(m_ptr, f);
        }

        T
        get()
        {
            return racy_initialize(m_ptr, f);
        }

    private:
        std::atomic<T> m_ptr{nullptr};
    };
} // namespace m

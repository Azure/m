// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <functional>
#include <mutex>
#include <shared_mutex>
#include <type_traits>

namespace m
{
#if M_HAS_CXX23
    template <typename LockT, typename MutexT, typename Fn, typename... Args>
    std::invoke_result_t<Fn, Args...>
    with_lock(MutexT&& m, Fn&& f, Args&&... args)
    {
        auto ul = LockT(m);

        return std::invoke_r<std::invoke_result_t<Fn, Args...>, Fn, Args...>(
            std::forward<Fn>(f), std::forward<Args>(args)...);
    }
#else
    template <typename LockT, typename MutexT, typename Fn, typename... Args>
    std::invoke_result_t<Fn, Args...>
    with_lock(MutexT&& m, Fn&& f, Args&&... args)
    {
        auto ul = LockT(m);

        return std::invoke(std::forward<Fn>(f), std::forward<Args>(args)...);
    }
#endif

    template <typename MutexT, typename Fn, typename... Args>
    std::invoke_result_t<Fn, Args...>
    with_unique_lock(MutexT&& m, Fn&& f, Args&&... args)
    {
        return with_lock<std::unique_lock<std::remove_reference_t<MutexT>>, MutexT, Fn, Args...>(
            std::forward<MutexT>(m), std::forward<Fn>(f), std::forward<Args>(args)...);
    }

    template <typename MutexT, typename Fn, typename... Args>
    std::invoke_result_t<Fn, Args...>
    with_shared_lock(MutexT&& m, Fn&& f, Args&&... args)
    {
        return with_lock<std::shared_lock<std::remove_reference_t<MutexT>>, MutexT, Fn, Args...>(
            std::forward<MutexT>(m), std::forward<Fn>(f), std::forward<Args>(args)...);
    }

} // namespace m
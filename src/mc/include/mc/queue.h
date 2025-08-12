// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <atomic>
#include <compare>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include <concepts>
#include <deque>
#include <optional>

namespace mc
{
    template <typename T>
        requires(std::regular<T>)
    class queue
    {
    public:
        using value_type = T;

        queue() = default;
        queue(queue const& other): m_deque(other.m_deque) {}
        queue(queue&& other) noexcept
        {
            using std::swap;
            swap(m_deque, other.m_deque);
        }
        ~queue() = default;

        queue&
        operator=(queue const& other)
        {
            if (this != &other)
            {
                queue temp(other);
                using std::swap;
                swap(temp, *this);
            }
            return *this;
        }

        queue&
        operator=(queue&& other) noexcept
        {
            if (this != &other)
            {
                using std::swap;
                swap(other, *this);
            }

            return *this;
        }

        std::optional<T>
        pop() noexcept
        {
            if (m_deque.empty())
                return std::nullopt;

            std::optional<T> retval{m_deque.front()};
            m_deque.pop_front();
            return retval;
        }

        template <typename Fn, typename... Args>
            requires(std::invocable<Fn, value_type const&, Args...>)
        bool
        pop(Fn&& fn, Args&&... args)
        {
            if (m_deque.empty())
                return false;

            std::invoke(std::forward<Fn>(fn),
                        const_cast<value_type const&>(m_deque.front()),
                        std::forward<Args>(args)...);

            m_deque.pop_front();

            return true;
        }

        template <typename... Args>
        value_type&
        push(Args&&... args)
        {
            return m_deque.emplace_back(std::forward<Args>(args)...);
        }

        bool
        empty() noexcept
        {
            return m_deque.empty();
        }

    private:
        std::deque<T> m_deque;
    };
} // namespace mc

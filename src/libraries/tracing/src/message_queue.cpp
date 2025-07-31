// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/message.h>
#include <m/tracing/message_queue.h>
#include <m/utility/compiler.h>

namespace m::tracing
{
    message_queue::message_queue() {}

    bool
    message_queue::empty() const noexcept
    {
        auto l = std::unique_lock(m_mutex);
        return m_queue.empty();
    }

    envelope
    message_queue::try_dequeue() noexcept
    {
        auto l = std::unique_lock(m_mutex);

        if (m_queue.empty())
            return envelope(this);

        envelope result(this);
        using std::swap;
        swap(result, m_queue.front());
        m_queue.pop();
        return result;
    }

    envelope
    message_queue::dequeue() noexcept
    {
        auto l = std::unique_lock(m_mutex);

        if (m_queue.empty())
            wait_for_nonempty(l);

        envelope result(this);
        using std::swap;
        swap(result, m_queue.front());
        m_queue.pop();
        return result;
    }

    std::optional<envelope>
    message_queue::wakeable_dequeue() noexcept
    {
        auto l = std::unique_lock(m_mutex);

        // The thing that's different from the normal pop() is that
        // we only wait once.
        if (m_queue.empty())
        {
            m_cv.wait(l);
        }

        if (!m_queue.empty())
        {
            envelope result(this);
            using std::swap;
            swap(result, m_queue.front());
            m_queue.pop();
            return result;
        }

        return std::nullopt;
    }

    void
    message_queue::wait() noexcept
    {
        auto l = std::unique_lock(m_mutex);

        if (m_queue.empty())
            m_cv.wait(l);
    }

    void
    message_queue::wake_waiters() noexcept
    {
        // In some world, we might see if we need to wake anyone
        // but in fact, we can just tell the cv to wake anyone.
        m_cv.notify_all();
    }

    void
    message_queue::enqueue(m::not_null<message*> msg) noexcept
    {
        auto l = std::unique_lock(m_mutex);
        m_queue.push(envelope(this, msg));

        l.unlock();

        m_cv.notify_all();
    }

    void
    message_queue::enqueue(envelope const& e) noexcept
    {
        enqueue(e.message());
    }

    envelope
    message_queue::allocate_message(event_kind kind)
    {
        envelope env = dequeue();
        env.message()->kind(kind);
        return env;
    }

    void
    message_queue::deallocate_message(m::not_null<message*> msg) noexcept
    {
        enqueue(msg);
    }

    M_NOINLINE void
    message_queue::wait_for_nonempty(std::unique_lock<std::mutex>& lock) noexcept
    {
        while (m_queue.empty())
            m_cv.wait(lock);
    }

} // namespace m::tracing

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/tracing.h>

namespace m::tracing
{
    message_queue::message_queue(): m_waiter_count{} {}

    bool
    message_queue::empty() const
    {
        auto l = std::unique_lock(m_mutex);
        return m_queue.empty();
    }

    envelope
    message_queue::try_dequeue()
    {
        auto l = std::unique_lock(m_mutex);
        if (m_queue.empty())
            return envelope();

        envelope   result;
        using std::swap;
        swap(result, m_queue.front());
        m_queue.pop();
        return result;
    }

    envelope
    message_queue::dequeue()
    {
        auto l = std::unique_lock(m_mutex);

        while (m_queue.empty())
        {
            m_waiter_count++;
            m_cv.wait(l);
            m_waiter_count--;
        }

        envelope   result;
        using std::swap;
        swap(result, m_queue.front());
        m_queue.pop();
        return result;
    }

    envelope
    message_queue::wakeable_dequeue()
    {
        auto l = std::unique_lock(m_mutex);

        // The thing that's different from the normal pop() is that
        // we only wait once.
        if (m_queue.empty())
        {
            m_waiter_count++;
            m_cv.wait(l);
            m_waiter_count--;
        }

        if (!m_queue.empty())
        {
            envelope   result;
            using std::swap;
            swap(result, m_queue.front());
            m_queue.pop();
            return result;
        }

        return envelope{};
    }

    void
        message_queue::wait()
    {
        auto l = std::unique_lock(m_mutex);

        if (m_queue.empty())
        {
            m_waiter_count++;
            m_cv.wait(l);
            m_waiter_count--;
        }
    }

    void
    message_queue::wake_waiters()
    {
        // In some world, we might see if we need to wake anyone
        // but in fact, we can just tell the cv to wake anyone.
        m_cv.notify_all();
    }

    void
    message_queue::enqueue(m::not_null<message*> msg)
    {
        bool wake = false;

        {
            auto l = std::unique_lock(m_mutex);
            m_queue.push(envelope(msg));
            if (m_waiter_count != 0)
                wake = true;
        }

        if (wake)
            m_cv.notify_all();
    }


    void
    message_queue::enqueue(envelope& e)
    {
        bool wake = false;

        {
            auto l = std::unique_lock(m_mutex);
            m_queue.push(std::move(e));
            if (m_waiter_count != 0)
                wake = true;
        }

        if (wake)
            m_cv.notify_all();
    }

} // namespace m::tracing

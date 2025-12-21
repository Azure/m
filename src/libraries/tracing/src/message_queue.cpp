// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/imessage.h>
#include <m/tracing/message.h>
#include <m/tracing/message_queue.h>
#include <m/utility/compiler.h>

#include "tracing_internal.h"

namespace m::tracing
{
    using m::tracing_impl::tr_frame;

    message_queue::message_queue() {}

    bool
    message_queue::empty() const noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        auto l = std::unique_lock(m_mutex);
        return frame.succeeded(m_queue.empty());
    }

    std::size_t
    message_queue::size() const noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        auto l = std::unique_lock(m_mutex);
        return frame.succeeded(m_queue.size());
    }

    envelope
    message_queue::try_dequeue() noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        auto     l = std::unique_lock(m_mutex);

        if (m_queue.empty())
        {
            frame.write(L"Queue was empty, returning an empty envelope");
            return frame.succeeded(envelope(this));
        }

        envelope result(this);
        using std::swap;
        swap(result, m_queue.front());
        m_queue.pop();
        frame.write(L"After dequeue, queue size is {}", m_queue.size());
        return frame.succeeded(std::move(result));
    }

    envelope
    message_queue::dequeue() noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        auto     l = std::unique_lock(m_mutex);

        if (m_queue.empty())
        {
            frame.write(L"Waiting for queue to become non-empty");
            wait_for_nonempty(l);
        }

        envelope result(this);
        using std::swap;
        swap(result, m_queue.front());
        m_queue.pop();
        frame.write(L"After dequeue, queue size is {}", m_queue.size());
        return frame.succeeded(std::move(result));
    }

    std::optional<envelope>
    message_queue::wakeable_dequeue() noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        auto     l = std::unique_lock(m_mutex);

        // The thing that's different from the normal pop() is that
        // we only wait once.
        if (m_queue.empty())
        {
            frame.write(L"Queue empty, waiting once to try to get an entry");
            m_cv.wait(l);
        }

        if (!m_queue.empty())
        {
            frame.write(L"Queue not empty!");
            envelope result(this);
            using std::swap;
            swap(result, m_queue.front());
            m_queue.pop();
            frame.write(L"Leaving queue with {} entries left", m_queue.size());
            return frame.succeeded(std::move(result));
        }

        frame.write(L"Queue empty, return std::nullopt");
        return frame.succeeded(std::nullopt);
    }

    void
    message_queue::wait() noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        auto     l = std::unique_lock(m_mutex);

        if (m_queue.empty())
        {
            frame.write(L"Queue is empty, waiting");
            m_cv.wait(l);
            frame.write(L"Woke from wait, queue now has {} entries", m_queue.size());
        }
        frame.succeeded();
    }

    void
    message_queue::wake_waiters() noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        // In some world, we might see if we need to wake anyone
        // but in fact, we can just tell the cv to wake anyone.
        m_cv.notify_all();
        frame.succeeded();
    }

    void
    message_queue::enqueue(m::not_null<imessage*> msg) noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        {
            auto l = std::unique_lock(m_mutex);
            m_queue.push(envelope(this, msg));
            frame.write(L"Message queue now has {} messages after push", m_queue.size());
        }
        m_cv.notify_all();
        frame.succeeded();
    }

    void
    message_queue::enqueue(envelope const& e) noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        enqueue(e.message());
        frame.succeeded();
    }

    envelope
    message_queue::allocate_message(event_kind kind)
    {
        tr_frame frame(__FUNCTION__, this);
        envelope env = dequeue();
        env.message()->kind(kind);
        return frame.succeeded(std::move(env));
    }

    void
    message_queue::deallocate_message(m::not_null<imessage*> msg) noexcept
    {
        tr_frame frame(__FUNCTION__, this);
        enqueue(msg);
        frame.succeeded();
    }

    M_NOINLINE void
    message_queue::wait_for_nonempty(std::unique_lock<std::mutex>& lock) noexcept
    {
        tr_frame frame(__FUNCTION__, this);

        while (m_queue.empty())
        {
            frame.write(L"Waiting because the message queue is empty");
            m_cv.wait(lock);
            frame.write(L"Message queue now has {} messages after wait", m_queue.size());
        }

        frame.succeeded();
    }

} // namespace m::tracing

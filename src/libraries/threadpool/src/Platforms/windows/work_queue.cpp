// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/exception/exception.h>
#include <m/thread_description/thread_description.h>
#include <m/threadpool/threadpool.h>
#include <m/threadpool/work_item_state.h>
#include <m/threadpool/work_queue_execution_policy.h>

#include "../../work_queue_base.h"
#include "work_queue.h"

namespace m::threadpool_impl
{
    work_queue::work_queue(work_queue_execution_policy wqep, std::wstring description):
        m::threadpool_impl::work_queue_base(wqep, description)
    {}

    work_queue::~work_queue()
    {
        // If the owner did not call close(), drain here. Draining is idempotent
        // and, because callbacks hold no ownership of the queue, this destructor
        // can only run on the owning thread, never on a threadpool callback
        // thread, so the synchronous wait below cannot deadlock against itself.
        perform_platform_teardown();
    }

    void
    work_queue::on_new_work_item(std::shared_ptr<m::work_queue_impl::work_item> const&)
    {
        m_tp_work.submit();
    }

    void
    work_queue::perform_platform_initialization()
    {
        auto callback_context_ptr          = std::make_unique<callback_context>();
        callback_context_ptr->m_work_queue = this;

        auto wrk = win32::threadpool::tp_work(
            &work_queue::static_tp_work_callback, callback_context_ptr.get(), nullptr);

        using std::swap;

        swap(wrk, m_tp_work);
        swap(callback_context_ptr, m_callback_context);
    }

    void
    work_queue::perform_platform_teardown() noexcept
    {
        // Cancel pending callbacks and wait for any in-flight callback to
        // finish. After this returns no callback will touch this queue. Safe to
        // call repeatedly (close() then destructor).
        if (m_platform_initialized)
        {
            m_tp_work.wait_for_callbacks(true);
        }
    }

    void CALLBACK
    work_queue::static_tp_work_callback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK) noexcept
    {
        auto const cctx = reinterpret_cast<callback_context*>(context);

        cctx->m_work_queue->tp_work_callback();
    }

    void
    work_queue::tp_work_callback() noexcept
    {
        auto l =
            std::unique_lock(m_mutex, std::defer_lock); // hold off lock taking until below since we
                                                        // will be taking and releasing the lock

        l.lock();
        // If the queue is empty and we were notified, things are broken, no?
        M_INTERNAL_ERROR_CHECK(!m_ready_queue.empty());
        auto const wi = m_ready_queue.front();
        auto const id = wi->id();
        m_running_work_items.emplace(id, wi);
        m_ready_queue.pop_front();
        l.unlock();

        m_cv.notify_all();

        {
            m::thread_description td(wi->description());
            wi->work();
        }

        l.lock();
        m_running_work_items.erase(id);
        l.unlock();

        m_cv.notify_all();
    }

} // namespace m::threadpool_impl

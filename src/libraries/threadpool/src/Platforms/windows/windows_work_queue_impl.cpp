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

#include "../../work_queue_impl.h"
#include "windows_work_queue_impl.h"

namespace m::windows_threadpool_impl
{
    work_queue::work_queue(work_queue_execution_policy wqep, std::wstring description):
        m::threadpool_impl::work_queue(wqep, description)
    {}

    void
    work_queue::on_new_work_item(std::shared_ptr<m::work_queue_impl::work_item> const&)
    {
        ::SubmitThreadpoolWork(m_tp_work);
    }

    void
    work_queue::perform_platform_initialization()
    {
        auto callback_context_ptr          = std::make_unique<callback_context>();
        callback_context_ptr->m_work_queue = weak_from_this();

        auto wrk =
            tp_work(&work_queue::static_tp_work_callback, callback_context_ptr.get(), nullptr);

        using std::swap;

        swap(wrk, m_tp_work);
        // Yes this induces a cycle. Can be fixed by having an explicit
        // close() protocol or by having the pointer back to the queue
        // be a weak reference which is a performance problem for each
        // queue entry. Solvable/contained.
        swap(callback_context_ptr, m_callback_context);
    }

    void
    work_queue::static_tp_work_callback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_WORK) noexcept
    {
        auto const cctx = reinterpret_cast<callback_context*>(context);

        cctx->m_work_queue.lock()->tp_work_callback();
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
            auto const desc = wi->description();

            if (desc.size() != 0)
            {
                m::thread_description td(desc);

                wi->work();
            }
            else
                wi->work();
        }

        l.lock();
        m_running_work_items.erase(id);
        l.unlock();

        m_cv.notify_all();
    }

} // namespace m::windows_threadpool_impl

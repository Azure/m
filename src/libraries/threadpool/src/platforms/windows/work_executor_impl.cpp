// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/thread_description/thread_description.h>
#include <m/threadpool/threadpool.h>

#include "work_executor_impl.h"
#include "work_item_impl.h"

namespace m::threadpool_impl
{
    work_executor::work_executor(std::wstring_view description):
        m_description(description)
    {
        //
    }

    void
    work_executor::execute(m::not_null<m::work_item_source*> source) noexcept
    {
        m::thread_description td(std::format(L"Work item executor '{}'", m_description));

        for (;;)
        {
            auto opt_wi = source->try_get_next_work_item();

            // If we're out of work items, we're done, get outta here
            if (!opt_wi)
                return;

            auto wi = opt_wi.value();

            // the shared ptr to the interface isn't enough. We need the
            // implementation so we're going to dynamic_cast<> down.
            auto wip     = wi.get();
            auto wi_impl = dynamic_cast<m::threadpool_impl::work_item*>(wip);

            // If this is nullptr, somehow a work item other than "one of ours"
            // got into the queue!
            M_INTERNAL_ERROR_CHECK(wi_impl != nullptr);

            wi_impl->run();
        }
    }

} // namespace m::threadpool_impl

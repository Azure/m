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

    void
    work_queue::on_new_work_item(std::shared_ptr<m::work_queue_impl::work_item> const&)
    {
        M_NOT_IMPLEMENTED("sorry no linux work queue");
    }

    void
    work_queue::perform_platform_initialization()
    {
        M_NOT_IMPLEMENTED("sorry no linux work queue");
    }

} // namespace m::threadpool_impl

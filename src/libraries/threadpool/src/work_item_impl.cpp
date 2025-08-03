// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <optional>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/threadpool/threadpool.h>
#include <m/threadpool/work_item_state.h>
#include <m/threadpool/work_queue_execution_policy.h>

#include "work_queue_impl.h"

namespace m::work_queue_impl
{
    work_item::work_item():
        m_id(work_queue_impl::work_item_id_counter.fetch_add(1)),
        m_work_item_state(work_item_state::queued)
    {}

    work_item::work_item(std::wstring description):
        m_id(work_queue_impl::work_item_id_counter.fetch_add(1)),
        m_description(std::move(description)),
        m_work_item_state(work_item_state::queued)
    {}


} // namespace m::threadpool_impl

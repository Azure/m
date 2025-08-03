// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <m/chrono/chrono.h>
#include <m/threadpool/work_executor.h>
#include <m/threadpool/work_queue.h>

namespace m::threadpool_impl
{
    class work_executor : public m::work_executor
    {
    public:
        work_executor(std::wstring_view description);

    private:
        void
        execute(m::not_null<m::work_item_source*> source) noexcept override;

        std::mutex   m_mutex;
        std::wstring m_description;
    };
} // namespace m::threadpool_impl

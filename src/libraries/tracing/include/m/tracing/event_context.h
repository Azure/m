// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace m
{
    namespace tracing
    {
        struct event_context
        {
            event_context();
            event_context(event_context const&);
            ~event_context() = default;
            event_context&
            operator=(event_context const&);

            static event_context
            current();

            uint64_t                           m_process_id;
            std::thread::id                    m_thread_id;
            std::chrono::utc_clock::time_point m_time_point;
        };

        constexpr void
        swap(event_context&, event_context&) noexcept;
    } // namespace tracing
} // namespace m

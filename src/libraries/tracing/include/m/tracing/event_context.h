// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

#include <m/chrono/chrono.h>

namespace m
{
    namespace tracing
    {
        class event_context
        {
        public:
            event_context() noexcept;
            event_context(event_context const& other) noexcept;

            ~event_context() = default;

            event_context&
            operator=(event_context const& other) noexcept;

            friend constexpr void
            swap(event_context& l, event_context& r) noexcept
            {
                using std::swap;

                swap(l.m_os_process_id, r.m_os_process_id);
                swap(l.m_os_thread_id, r.m_os_thread_id);
                swap(l.m_time_point, r.m_time_point);
            }

            static event_context
            current();

            constexpr uint64_t
            os_process_id() const
            {
                return m_os_process_id;
            }

            constexpr uint64_t
            os_thread_id() const
            {
                return m_os_thread_id;
            }

            constexpr utc_time_point_type
            time_point() const
            {
                return m_time_point;
            }

        protected:
            uint32_t            m_os_process_id;
            uint32_t            m_os_thread_id;
            utc_time_point_type m_time_point;
        };

    } // namespace tracing
} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <string>
#include <string_view>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/tracing/tracing.h>

#include "pcwstr.h"
#include "win32.h"

namespace m::pil::impl::win32
{
    constexpr auto default_open_key_for_monitoring_retry_wait_duration =
        std::chrono::milliseconds(500);

    constexpr auto default_rkaa =
        m::pil::iregistry_monitor_change_notification::requeue_key_access_attempt{
            default_open_key_for_monitoring_retry_wait_duration};

    constexpr auto default_notify_change_value_retry_wait_duration = std::chrono::milliseconds(500);

    constexpr auto default_rcna =
        m::pil::iregistry_monitor_change_notification::requeue_change_notification_attempt{
            default_notify_change_value_retry_wait_duration};

    constexpr m::win32::registry::notify_filters
    flags_to_notify_filters(m::pil::iregistry_monitor::register_watch_flags flags)
    {
        m::win32::registry::notify_filters filters{};

        using enum m::pil::iregistry_monitor::register_watch_flags;

        if (!!(flags & key_changes))
            filters |= m::win32::registry::notify_filters::change_name;

        if (!!(flags & attribute_changes))
            filters |= m::win32::registry::notify_filters::change_attributes;

        if (!!(flags & value_changes))
            filters |= m::win32::registry::notify_filters::change_last_set;

        if (!!(flags & security_changes))
            filters |= m::win32::registry::notify_filters::change_security;

        return filters;
    }

    registry_monitor_token::registry_monitor_token(
        std::shared_ptr<m::work_queue>                      work_queue,
        m::pil::iregistry_monitor::register_watch_flags     flags,
        pil::key_path const&                          key_path,
        m::not_null<iregistry_monitor_change_notification*> change_notification_ptr):
        m_work_queue(std::move(work_queue)),
        m_flags(flags),
        m_filters(flags_to_notify_filters(flags)),
        m_state{},
        m_key_path(key_path),
        m_key_name(m_key_path.native()),
        m_event(m::win32::create_event_flags::manual_reset),
        m_tp_wait(&registry_monitor_token::registry_notification_wait_callback, this, nullptr),
        m_change_notification_ptr(change_notification_ptr),
        m_timer(threadpool->create_timer([this] {
            m::wtrace(m::tracing::event_kind::information,
                      L"{}: timer lambda fired at {}",
                      __FUNCTIONW__,
                      m_notification_time);
            utc_time_point when{};
            {
                auto l = std::unique_lock(m_mutex);
                when   = m_notification_time;
            }
            on_timer(m::locked, when);
        })),
        m_notification_timer(threadpool->create_timer([this]() {
            m::wtrace(m::tracing::event_kind::information,
                      L"{}: change notification timer lambda fired at {}",
                      __FUNCTIONW__,
                      m_notification_time);
            utc_time_point when{};
            {
                auto l = std::unique_lock(m_mutex);
                when   = m_notification_time;
            }
            m_change_notification_ptr->on_change(when, m_key_path);
        })),
        m_notification_time{}
    {
        using enum m::pil::iregistry_monitor::register_watch_flags;

        M_VALIDATE_FLAGS_PARAMETER(flags,
                                   attribute_changes | watch_subtree | key_changes | value_changes |
                                       security_changes);

        // Just going through the motion w.r.t. setting the m_state so that the
        // code is super clear about managing the state machine.
        //
        // The state indicates the *next* thing to do, so it's updated after successfully
        // completing a step. Or not.
        m_state = state::to_open_key;

        drive_state(m::locked, m::clock::now());
    }

    void
    registry_monitor_token::on_timer(m::locked_t, utc_time_point when) noexcept
    {
        drive_state(m::locked, when);
    }

    void
    registry_monitor_token::drive_state(m::locked_t, utc_time_point when) noexcept
    {
        for (;;)
        {
            if (drive_state_once(m::locked, when) == drive_results::waiting)
                break;
        }
    }

    registry_monitor_token::drive_results
    registry_monitor_token::drive_state_once(m::locked_t, utc_time_point when) noexcept
    {
        m::wtrace(m::tracing::event_kind::information,
                  L"{}: called at {} with state {}",
                  __FUNCTIONW__,
                  when,
                  m::to_underlying(m_state));
        switch (m_state)
        {
            using enum state;

            case to_open_key:
            {
                auto const ec = m_hkey.openq(pil_pk_to_win32_pk(m_key_path.root_key().value()),
                                             m_key_path.relative_path(),
                                             KEY_NOTIFY);

                if (!ec)
                {
                    m_state = state::to_notify_change_key;
                    return drive_results::not_waiting;
                }

                auto const rkaa =
                    m_change_notification_ptr->on_key_access_failure(when, m_key_path, ec);

                auto const dur = rkaa.value_or(default_rkaa).m_milliseconds;
                m_timer->set(dur);
                return drive_results::waiting;
            }

            case to_notify_change_key:
            {
                m_event.set_event_state(m::win32::event::event_state::reset);

                m_tp_wait.set_wait(m_event);

                auto const ec = m_hkey.notify_change_valueq(
                    !!(m_flags & m::pil::iregistry_monitor::register_watch_flags::watch_subtree),
                    m_filters,
                    m_event,
                    true);

                if (!ec)
                {
                    m_state = state::waiting;
                    return drive_results::waiting;
                }

                auto const rcna = m_change_notification_ptr->on_change_notification_attempt_failure(
                    when, m_key_path, ec);

                auto const dur = rcna.value_or(default_rcna).m_milliseconds;
                m_timer->set(dur);

                return drive_results::waiting;
            }

            case waiting:
            {
                // We have finished waiting. We have two critical things to do:
                //
                // 1. notify the client
                // 2. set the next wait cycle
                //
                m_notification_time = when;
                m_notification_timer->set(std::chrono::milliseconds(0));
                m_state = state::to_notify_change_key;
                return drive_results::not_waiting;
            }
            default: M_UNREACHABLE_CODE();
        }
    }

    void
    registry_monitor_token::registry_notification_wait_callback(PTP_CALLBACK_INSTANCE instance,
                                                                PVOID                 context,
                                                                PTP_WAIT              wait,
                                                                TP_WAIT_RESULT        wait_result)
    {
        std::ignore = instance;
        std::ignore = wait;
        // We are expecting either that the wait completed on the event, or at some point
        // we perhaps support timeouts? At the point of coding this, there are not waits-
        // with-timeout but there's no harm in coding for it at this time to avoid having
        // to go through and rework things later.
        M_INTERNAL_ERROR_CHECK((wait_result == WAIT_OBJECT_0) || (wait_result == WAIT_TIMEOUT));
        auto const this_ptr = reinterpret_cast<registry_monitor_token*>(context);
        this_ptr->on_registry_notification(wait_result == WAIT_TIMEOUT);
    }

    void
    registry_monitor_token::on_registry_notification(bool timed_out)
    {
        std::ignore     = timed_out;
        auto const when = std::chrono::utc_clock::now();
        auto       l    = std::unique_lock(m_mutex);
        drive_state(m::locked, when);
    }

} // namespace m::pil::impl::win32

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>
#include <m/pil/registry.h>
#include <m/strings/convert.h>
#include <m/utility/make_span.h>

#include "passthrough.h"

namespace m::pil::impl::passthrough
{
    registry_monitor_change_notification_wrapper::registry_monitor_change_notification_wrapper(
        m::not_null<iregistry_monitor_change_notification*> change_notification):
        m_change_notification(change_notification)
    {}

    void
    registry_monitor_change_notification_wrapper::on_begin(utc_time_point when)
    {
        m_change_notification->on_begin(when);
    }

    std::optional<pil::iregistry_monitor_change_notification::requeue_key_access_attempt>
    registry_monitor_change_notification_wrapper::on_key_access_failure(utc_time_point       when,
                                                                        pil::key_path const& key,
                                                                        std::system_error const& ec)
    {
        return m_change_notification->on_key_access_failure(when, key, ec);
    }

    std::optional<pil::iregistry_monitor_change_notification::requeue_change_notification_attempt>
    registry_monitor_change_notification_wrapper::on_change_notification_attempt_failure(
        utc_time_point           when,
        pil::key_path const&     key,
        std::system_error const& ec)
    {
        return m_change_notification->on_change_notification_attempt_failure(when, key, ec);
    }

    void
    registry_monitor_change_notification_wrapper::on_change(utc_time_point       when,
                                                            pil::key_path const& key)
    {
        m_change_notification->on_change(when, key);
    }

    void
    registry_monitor_change_notification_wrapper::on_cancelled(utc_time_point when)
    {
        m_change_notification->on_cancelled(when);
    }

} // namespace m::pil::impl::passthrough

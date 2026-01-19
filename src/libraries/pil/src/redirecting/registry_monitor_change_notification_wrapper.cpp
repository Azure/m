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

#include "redirecting.h"

namespace m::pil::impl::redirecting
{
    registry_monitor_change_notification_wrapper::registry_monitor_change_notification_wrapper(
        m::not_null<iregistry_monitor_change_notification*> change_notification,
        std::shared_ptr<redirector> const&                  redir):
        m_change_notification(change_notification), m_redirector(redir)
    {}

    void
    registry_monitor_change_notification_wrapper::on_begin(utc_time_point_type const& when)
    {
        m_change_notification->on_begin(when);
    }

    std::optional<pil::iregistry_monitor_change_notification::requeue_key_access_attempt>
    registry_monitor_change_notification_wrapper::on_key_access_failure(
        utc_time_point_type const& when,
        pil::key_path const&       key,
        std::system_error const&   ec)
    {
        auto const mapped_key = m_redirector->map_private_to_public(key);
        return m_change_notification->on_key_access_failure(when, mapped_key, ec);
    }

    std::optional<pil::iregistry_monitor_change_notification::requeue_change_notification_attempt>
    registry_monitor_change_notification_wrapper::on_change_notification_attempt_failure(
        utc_time_point_type const& when,
        pil::key_path const&       key,
        std::system_error const&   ec)
    {
        auto const mapped_key = m_redirector->map_private_to_public(key);
        return m_change_notification->on_change_notification_attempt_failure(when, mapped_key, ec);
    }

    void
    registry_monitor_change_notification_wrapper::on_change(utc_time_point_type const& when,
                                                            pil::key_path const&       key)
    {
        auto const mapped_key = m_redirector->map_private_to_public(key);
        m_change_notification->on_change(when, mapped_key);
    }

    void
    registry_monitor_change_notification_wrapper::on_cancelled(utc_time_point_type const& when)
    {
        m_change_notification->on_cancelled(when);
    }

} // namespace m::pil::impl::redirecting

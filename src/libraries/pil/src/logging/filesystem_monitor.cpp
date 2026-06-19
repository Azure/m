// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>

#include <m/error_handling/macros.h>
#include <m/pil/filesystem.h>
#include <m/pil/pil.h>
#include <m/pil/platform.h>

#include "logging.h"

namespace m::pil::impl::logging
{
    filesystem_monitor::filesystem_monitor(
        std::shared_ptr<ifilesystem_monitor> const& underlying_filesystem_monitor):
        m_underlying_filesystem_monitor(underlying_filesystem_monitor)
    {}

    ifilesystem_monitor::register_watch_disposition
    filesystem_monitor::register_watch(
        register_watch_flags                                  flags,
        file_path const&                                      directory,
        m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr,
        std::unique_ptr<ifilesystem_monitor_token>&           returned_ptr)
    {
        returned_ptr.reset();

        auto notification_wrapper =
            std::unique_ptr<filesystem_monitor_change_notification_wrapper>(
                new filesystem_monitor_change_notification_wrapper(change_notification_ptr));

        auto d = m_underlying_filesystem_monitor->register_watch(
            flags, directory, notification_wrapper.get(), notification_wrapper->m_underlying_token);

        returned_ptr.reset(notification_wrapper.release());

        return d;
    }

    filesystem_monitor_change_notification_wrapper::
        filesystem_monitor_change_notification_wrapper(
            m::not_null<ifilesystem_monitor_change_notification*> change_notification):
        m_change_notification(change_notification)
    {}

    void
    filesystem_monitor_change_notification_wrapper::on_begin(utc_time_point_type const& when)
    {
        m_change_notification->on_begin(when);
    }

    std::optional<pil::ifilesystem_monitor_change_notification::requeue_directory_access_attempt>
    filesystem_monitor_change_notification_wrapper::on_directory_access_failure(
        utc_time_point_type const& when,
        file_path const&           directory,
        std::system_error const&   ec)
    {
        return m_change_notification->on_directory_access_failure(when, directory, ec);
    }

    std::optional<
        pil::ifilesystem_monitor_change_notification::requeue_change_notification_attempt>
    filesystem_monitor_change_notification_wrapper::on_change_notification_attempt_failure(
        utc_time_point_type const& when,
        file_path const&           directory,
        std::system_error const&   ec)
    {
        return m_change_notification->on_change_notification_attempt_failure(when, directory, ec);
    }

    void
    filesystem_monitor_change_notification_wrapper::on_change(utc_time_point_type const& when,
                                                              file_path const&       directory,
                                                              filesystem_change_kind kind,
                                                              file_path const&       entry_name)
    {
        m_change_notification->on_change(when, directory, kind, entry_name);
    }

    void
    filesystem_monitor_change_notification_wrapper::on_cancelled(utc_time_point_type const& when)
    {
        m_change_notification->on_cancelled(when);
    }

} // namespace m::pil::impl::logging

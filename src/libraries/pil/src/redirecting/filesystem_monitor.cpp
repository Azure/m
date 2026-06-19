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

#include "redirecting.h"
#include "rundown.h"

namespace m::pil::impl::redirecting
{
    filesystem_monitor::filesystem_monitor(
        std::shared_ptr<ifilesystem_monitor> const& underlying_filesystem_monitor,
        std::shared_ptr<fs_redirector> const&       redir):
        m_underlying_filesystem_monitor(underlying_filesystem_monitor), m_redirector(redir)
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
                new filesystem_monitor_change_notification_wrapper(change_notification_ptr,
                                                                   m_redirector));

        auto mapped_directory = m_redirector->map_public_to_private(directory);

        auto d = m_underlying_filesystem_monitor->register_watch(
            flags,
            mapped_directory,
            notification_wrapper.get(),
            notification_wrapper->m_underlying_token);

        returned_ptr.reset(notification_wrapper.release());

        return d;
    }

    filesystem_monitor_change_notification_wrapper::
        filesystem_monitor_change_notification_wrapper(
            m::not_null<ifilesystem_monitor_change_notification*> change_notification,
            std::shared_ptr<fs_redirector> const&                 redir):
        m_change_notification(change_notification), m_redirector(redir)
    {}

    filesystem_monitor_change_notification_wrapper::
        ~filesystem_monitor_change_notification_wrapper()
    {
        //
        // During process rundown, releasing the underlying directory-watch token
        // would drive its threadpool wait/timer teardown
        // (WaitForThreadpool*Callbacks) against worker threads the OS has already
        // destroyed -- a guaranteed hang -- and would trace through late-shutdown
        // infrastructure. Leak the token instead; the OS reclaims everything at
        // exit. On a normal release, or a FreeLibrary unload while the process
        // lives on, process_rundown_in_progress() is false and the token tears
        // down cleanly.
        //
        if (process_rundown_in_progress())
            (void)m_underlying_token.release();
    }

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
        auto const mapped_directory = m_redirector->map_private_to_public(directory);
        return m_change_notification->on_directory_access_failure(when, mapped_directory, ec);
    }

    std::optional<
        pil::ifilesystem_monitor_change_notification::requeue_change_notification_attempt>
    filesystem_monitor_change_notification_wrapper::on_change_notification_attempt_failure(
        utc_time_point_type const& when,
        file_path const&           directory,
        std::system_error const&   ec)
    {
        auto const mapped_directory = m_redirector->map_private_to_public(directory);
        return m_change_notification->on_change_notification_attempt_failure(
            when, mapped_directory, ec);
    }

    void
    filesystem_monitor_change_notification_wrapper::on_change(utc_time_point_type const& when,
                                                              file_path const&       directory,
                                                              filesystem_change_kind kind,
                                                              file_path const&       entry_name)
    {
        auto const mapped_directory = m_redirector->map_private_to_public(directory);
        m_change_notification->on_change(when, mapped_directory, kind, entry_name);
    }

    void
    filesystem_monitor_change_notification_wrapper::on_cancelled(utc_time_point_type const& when)
    {
        m_change_notification->on_cancelled(when);
    }

} // namespace m::pil::impl::redirecting

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

#include "passthrough.h"

namespace m::pil::impl::passthrough
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

} // namespace m::pil::impl::passthrough

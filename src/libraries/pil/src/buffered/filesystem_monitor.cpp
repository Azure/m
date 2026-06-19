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

#include "buffered.h"

namespace m::pil::impl::buffered
{
    filesystem_monitor::filesystem_monitor(
        std::shared_ptr<ifilesystem_monitor> const& underlying_filesystem_monitor):
        m_underlying_filesystem_monitor(underlying_filesystem_monitor)
    {}

    filesystem_monitor::filesystem_monitor(
        std::shared_ptr<ifilesystem_monitor>&& underlying_filesystem_monitor) noexcept:
        m_underlying_filesystem_monitor(std::move(underlying_filesystem_monitor))
    {}

    ifilesystem_monitor::register_watch_disposition
    filesystem_monitor::register_watch(
        register_watch_flags                                  flags,
        file_path const&                                      directory,
        m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr,
        std::unique_ptr<ifilesystem_monitor_token>&           returned_ptr)
    {
        std::ignore = change_notification_ptr;
        std::ignore = directory;
        std::ignore = flags;
        returned_ptr.reset();

        // A buffered filesystem is a sealed snapshot: it does not observe live
        // change, so change notification is not implemented (mirrors the
        // buffered registry monitor).
        M_NOT_IMPLEMENTED("buffered filesystem change notification not implemented");

        // return register_watch_disposition{};
    }

} // namespace m::pil::impl::buffered

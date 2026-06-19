// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <new>

#include <m/error_handling/macros.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/filesystem.h>

#include "win32.h"

namespace m::pil::impl::win32
{
    filesystem_monitor::filesystem_monitor(std::shared_ptr<m::work_queue> wq):
        m_work_queue(std::move(wq))
    {}

    ifilesystem_monitor::register_watch_disposition
    filesystem_monitor::register_watch(
        register_watch_flags                                  flags,
        file_path const&                                      directory,
        m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr,
        std::unique_ptr<ifilesystem_monitor_token>&           returned_ptr)
    {
        returned_ptr.reset();

        M_VALIDATE_FLAGS_PARAMETER(
            flags,
            register_watch_flags::watch_subtree | register_watch_flags::file_name_changes |
                register_watch_flags::directory_name_changes |
                register_watch_flags::attribute_changes | register_watch_flags::size_changes |
                register_watch_flags::last_write_changes |
                register_watch_flags::last_access_changes |
                register_watch_flags::creation_changes | register_watch_flags::security_changes);

        auto l = std::unique_lock(m_mutex);

        auto wq = m_work_queue;

        l.unlock();

        returned_ptr.reset(
            new filesystem_monitor_token(wq, flags, directory, change_notification_ptr));

        return register_watch_disposition{};
    }
} // namespace m::pil::impl::win32

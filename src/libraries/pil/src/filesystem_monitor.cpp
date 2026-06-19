// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <memory>
#include <mutex>

#include <m/pil/filesystem.h>

namespace m::pil
{
    std::unique_ptr<ifilesystem_monitor_token>
    filesystem_monitor::do_register_watch(
        register_watch_flags                                  flags,
        file_path const&                                      directory,
        m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr)
    {
        M_VALIDATE_FLAGS_PARAMETER(
            flags,
            register_watch_flags::watch_subtree | register_watch_flags::file_name_changes |
                register_watch_flags::directory_name_changes |
                register_watch_flags::attribute_changes | register_watch_flags::size_changes |
                register_watch_flags::last_write_changes |
                register_watch_flags::last_access_changes |
                register_watch_flags::creation_changes | register_watch_flags::security_changes);

        ifilesystem_monitor::register_watch_flags inner_flags{};

        if (!!(flags & register_watch_flags::watch_subtree))
            inner_flags |= ifilesystem_monitor::register_watch_flags::watch_subtree;

        if (!!(flags & register_watch_flags::file_name_changes))
            inner_flags |= ifilesystem_monitor::register_watch_flags::file_name_changes;

        if (!!(flags & register_watch_flags::directory_name_changes))
            inner_flags |= ifilesystem_monitor::register_watch_flags::directory_name_changes;

        if (!!(flags & register_watch_flags::attribute_changes))
            inner_flags |= ifilesystem_monitor::register_watch_flags::attribute_changes;

        if (!!(flags & register_watch_flags::size_changes))
            inner_flags |= ifilesystem_monitor::register_watch_flags::size_changes;

        if (!!(flags & register_watch_flags::last_write_changes))
            inner_flags |= ifilesystem_monitor::register_watch_flags::last_write_changes;

        if (!!(flags & register_watch_flags::last_access_changes))
            inner_flags |= ifilesystem_monitor::register_watch_flags::last_access_changes;

        if (!!(flags & register_watch_flags::creation_changes))
            inner_flags |= ifilesystem_monitor::register_watch_flags::creation_changes;

        if (!!(flags & register_watch_flags::security_changes))
            inner_flags |= ifilesystem_monitor::register_watch_flags::security_changes;

        auto l = std::unique_lock(m_mutex);

        return m_ifilesystem_monitor->register_watch(
            inner_flags, directory, change_notification_ptr);
    }

} // namespace m::pil

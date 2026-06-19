// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

#include <m/cast/to.h>
#include <m/errors/errors.h>
#include <m/pil/common.h>
#include <m/pil/filesystem.h>

#include "pcwstr.h"
#include "win32.h"

namespace m::pil::impl::win32
{
    namespace
    {
        // The share mode used when opening the watched directory; permitting
        // concurrent read/write/delete sharing keeps the watch handle from
        // blocking operations on the directory it observes.
        constexpr DWORD monitor_share_all =
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

        // Default backoff before re-attempting to open the directory for
        // monitoring after an access failure.
        constexpr auto default_open_directory_retry_wait_duration =
            std::chrono::milliseconds(500);

        constexpr auto default_rdaa =
            m::pil::ifilesystem_monitor_change_notification::requeue_directory_access_attempt{
                default_open_directory_retry_wait_duration};

        // Default backoff before re-attempting to arm the change-notification
        // read after a failure.
        constexpr auto default_read_changes_retry_wait_duration = std::chrono::milliseconds(500);

        constexpr auto default_rcna =
            m::pil::ifilesystem_monitor_change_notification::requeue_change_notification_attempt{
                default_read_changes_retry_wait_duration};

        // Maps the surface watch flags onto the ReadDirectoryChangesW filter
        // mask. When no category bits are selected a comprehensive default is
        // used so the zero-flags convenience overload observes create / rename /
        // delete (the categories the tests exercise).
        DWORD
        flags_to_notify_filter(m::pil::ifilesystem_monitor::register_watch_flags flags)
        {
            using enum m::pil::ifilesystem_monitor::register_watch_flags;

            DWORD filter = 0;

            if (!!(flags & file_name_changes))
                filter |= FILE_NOTIFY_CHANGE_FILE_NAME;
            if (!!(flags & directory_name_changes))
                filter |= FILE_NOTIFY_CHANGE_DIR_NAME;
            if (!!(flags & attribute_changes))
                filter |= FILE_NOTIFY_CHANGE_ATTRIBUTES;
            if (!!(flags & size_changes))
                filter |= FILE_NOTIFY_CHANGE_SIZE;
            if (!!(flags & last_write_changes))
                filter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
            if (!!(flags & last_access_changes))
                filter |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
            if (!!(flags & creation_changes))
                filter |= FILE_NOTIFY_CHANGE_CREATION;
            if (!!(flags & security_changes))
                filter |= FILE_NOTIFY_CHANGE_SECURITY;

            if (filter == 0)
                filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                         FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE |
                         FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;

            return filter;
        }

        // Maps a Win32 FILE_ACTION_* code to the surface change kind. Unknown
        // actions yield no value and are skipped.
        std::optional<filesystem_change_kind>
        action_to_change_kind(DWORD action)
        {
            switch (action)
            {
                case FILE_ACTION_ADDED: return filesystem_change_kind::added;
                case FILE_ACTION_REMOVED: return filesystem_change_kind::removed;
                case FILE_ACTION_MODIFIED: return filesystem_change_kind::modified;
                case FILE_ACTION_RENAMED_OLD_NAME: return filesystem_change_kind::renamed_old_name;
                case FILE_ACTION_RENAMED_NEW_NAME: return filesystem_change_kind::renamed_new_name;
                default: return std::nullopt;
            }
        }
    } // namespace

    filesystem_monitor_token::filesystem_monitor_token(
        std::shared_ptr<m::work_queue>                        work_queue,
        m::pil::ifilesystem_monitor::register_watch_flags     flags,
        file_path const&                                      directory,
        m::not_null<ifilesystem_monitor_change_notification*> change_notification_ptr):
        m_work_queue(std::move(work_queue)),
        m_flags(flags),
        m_notify_filter(flags_to_notify_filter(flags)),
        m_watch_subtree(
            !!(flags & m::pil::ifilesystem_monitor::register_watch_flags::watch_subtree)),
        m_state{},
        m_directory_path(directory),
        m_directory_win32_path(to_win32_path(directory)),
        m_directory_handle{},
        m_event(m::win32::create_event_flags::manual_reset),
        m_overlapped{},
        m_buffer(notification_buffer_byte_count),
        m_tp_wait(&filesystem_monitor_token::filesystem_notification_wait_callback, this, nullptr),
        m_change_notification_ptr(change_notification_ptr),
        m_timer(threadpool->create_timer([this] {
            auto l = std::unique_lock(m_mutex);
            if (m_shutting_down)
                return;
            on_timer(m::locked, m_notification_time);
        })),
        m_notification_timer(threadpool->create_timer([this] {
            utc_time_point_type                                      when{};
            std::vector<std::pair<filesystem_change_kind, file_path>> changes;
            {
                auto l  = std::unique_lock(m_mutex);
                if (m_shutting_down)
                    return;
                when    = m_notification_time;
                changes = std::move(m_pending_changes);
                m_pending_changes.clear();
            }
            for (auto const& change: changes)
                m_change_notification_ptr->on_change(
                    when, m_directory_path, change.first, change.second);
        })),
        m_notification_time{}
    {
        using enum m::pil::ifilesystem_monitor::register_watch_flags;

        M_VALIDATE_FLAGS_PARAMETER(flags,
                                   watch_subtree | file_name_changes | directory_name_changes |
                                       attribute_changes | size_changes | last_write_changes |
                                       last_access_changes | creation_changes | security_changes);

        // The state indicates the *next* thing to do; it is advanced only after
        // a step completes successfully.
        m_state = state::to_open_directory;

        auto l = std::unique_lock(m_mutex);
        drive_state(m::locked, m::clock_type::now());
    }

    filesystem_monitor_token::~filesystem_monitor_token()
    {
        // Signal shutdown first: any wait or timer callback that wins m_mutex
        // from here on observes the flag and returns without re-arming the read
        // or scheduling a timer, so the quiesce steps below drain to a fixed
        // point rather than racing an in-flight callback that re-arms work.
        {
            auto l = std::unique_lock(m_mutex);
            m_shutting_down = true;
        }

        // Quiesce the threadpool wait while every member it touches is still
        // alive: cancel any in-flight read, then disarm the wait. reset()
        // disarms the wait, waits for any in-flight callback to finish, and
        // closes the wait object, so after it returns no wait callback can
        // fire. The wait callback is what arms the timers, so draining it first
        // guarantees no new timer is scheduled past this point.
        if (m_directory_handle.is_valid())
            ::CancelIoEx(m_directory_handle.get(), &m_overlapped);

        m_tp_wait.reset();

        // Now quiesce the timer callbacks while the members they touch
        // (m_pending_changes, m_change_notification_ptr, m_directory_path, ...)
        // are still alive. Member destruction runs in reverse declaration
        // order, which would free m_pending_changes before m_notification_timer;
        // a timer callback still in flight at that moment would move from a
        // destroyed deque -- an intermittent use-after-free in release builds,
        // or a teardown hang when the timer destructor blocks on a callback
        // that is itself blocked. Resetting the timers here drains their
        // callbacks up front, closing that window.
        m_notification_timer.reset();
        m_timer.reset();
    }

    void
    filesystem_monitor_token::on_timer(m::locked_t, utc_time_point_type const& when) noexcept
    {
        drive_state(m::locked, when);
    }

    void
    filesystem_monitor_token::drive_state(m::locked_t, utc_time_point_type const& when) noexcept
    {
        // Once teardown has begun, do nothing: arming a read or a timer here
        // would schedule work that races member destruction.
        if (m_shutting_down)
            return;

        for (;;)
        {
            if (drive_state_once(m::locked, when) == drive_results::waiting)
                break;
        }
    }

    filesystem_monitor_token::drive_results
    filesystem_monitor_token::drive_state_once(m::locked_t,
                                               utc_time_point_type const& when) noexcept
    {
        switch (m_state)
        {
            using enum state;

            case to_open_directory:
            {
                auto const namez = pcwstr(std::u16string_view(m_directory_win32_path));

                HANDLE const raw = ::CreateFileW(namez,
                                                 FILE_LIST_DIRECTORY,
                                                 monitor_share_all,
                                                 nullptr,
                                                 OPEN_EXISTING,
                                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                                 nullptr);

                if (raw != INVALID_HANDLE_VALUE)
                {
                    m_directory_handle = m::win32::handle(raw);
                    m_state            = state::to_read_directory_changes;
                    return drive_results::not_waiting;
                }

                std::error_code const ec(static_cast<int>(::GetLastError()),
                                         std::system_category());

                auto const rdaa = m_change_notification_ptr->on_directory_access_failure(
                    when, m_directory_path, std::system_error(ec));

                auto const dur = rdaa.value_or(default_rdaa).m_milliseconds;
                m_timer->set(dur);
                return drive_results::waiting;
            }

            case to_read_directory_changes:
            {
                m_event.set_event_state(m::win32::event::event_state::reset);

                m_tp_wait.set_wait(m_event);

                m_overlapped        = OVERLAPPED{};
                m_overlapped.hEvent = m_event;

                BOOL const ok = ::ReadDirectoryChangesW(m_directory_handle.get(),
                                                        m_buffer.data(),
                                                        m::to<DWORD>(m_buffer.size()),
                                                        m_watch_subtree ? TRUE : FALSE,
                                                        m_notify_filter,
                                                        nullptr,
                                                        &m_overlapped,
                                                        nullptr);

                if (ok)
                {
                    m_state = state::waiting;
                    return drive_results::waiting;
                }

                std::error_code const ec(static_cast<int>(::GetLastError()),
                                         std::system_category());

                auto const rcna = m_change_notification_ptr->on_change_notification_attempt_failure(
                    when, m_directory_path, std::system_error(ec));

                auto const dur = rcna.value_or(default_rcna).m_milliseconds;
                m_timer->set(dur);
                return drive_results::waiting;
            }

            case waiting:
            {
                DWORD      bytes = 0;
                BOOL const ok =
                    ::GetOverlappedResult(m_directory_handle.get(), &m_overlapped, &bytes, FALSE);

                if (!ok)
                {
                    std::error_code const ec(static_cast<int>(::GetLastError()),
                                             std::system_category());

                    auto const rcna =
                        m_change_notification_ptr->on_change_notification_attempt_failure(
                            when, m_directory_path, std::system_error(ec));

                    auto const dur = rcna.value_or(default_rcna).m_milliseconds;
                    m_timer->set(dur);
                    return drive_results::waiting;
                }

                decode_notifications(m::locked, bytes);

                if (!m_pending_changes.empty())
                {
                    m_notification_time = when;
                    m_notification_timer->set(std::chrono::milliseconds(0));
                }

                // Re-arm the read for the next batch.
                m_state = state::to_read_directory_changes;
                return drive_results::not_waiting;
            }

            default: M_UNREACHABLE_CODE();
        }
    }

    void
    filesystem_monitor_token::decode_notifications(m::locked_t, std::size_t byte_count)
    {
        // A zero-byte result means the buffer overflowed and the changes were
        // lost; there is nothing to decode.
        if (byte_count == 0)
            return;

        std::byte const* const base   = m_buffer.data();
        std::size_t            offset = 0;

        for (;;)
        {
            auto const* const info =
                reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(base + offset);

            std::size_t const name_char_count = info->FileNameLength / sizeof(WCHAR);
            std::u16string_view const name_view(
                reinterpret_cast<char16_t const*>(info->FileName), name_char_count);

            if (auto const kind = action_to_change_kind(info->Action))
                m_pending_changes.emplace_back(*kind,
                                               file_path(file_path::view_type(name_view)));

            if (info->NextEntryOffset == 0)
                break;

            offset += info->NextEntryOffset;
        }
    }

    void __stdcall
    filesystem_monitor_token::filesystem_notification_wait_callback(PTP_CALLBACK_INSTANCE instance,
                                                                    PVOID                 context,
                                                                    PTP_WAIT              wait,
                                                                    TP_WAIT_RESULT wait_result)
    {
        std::ignore = instance;
        std::ignore = wait;

        M_INTERNAL_ERROR_CHECK((wait_result == WAIT_OBJECT_0) || (wait_result == WAIT_TIMEOUT));
        auto const this_ptr = reinterpret_cast<filesystem_monitor_token*>(context);
        this_ptr->on_filesystem_notification(wait_result == WAIT_TIMEOUT);
    }

    void
    filesystem_monitor_token::on_filesystem_notification(bool timed_out)
    {
        std::ignore     = timed_out;
        auto const when = std::chrono::utc_clock::now();
        auto       l    = std::unique_lock(m_mutex);
        drive_state(m::locked, when);
    }

} // namespace m::pil::impl::win32

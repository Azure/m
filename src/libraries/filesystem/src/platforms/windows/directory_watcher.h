// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <Windows.h>

#include <m/cast/to.h>
#include <m/chrono/chrono.h>
#include <m/filesystem/filesystem.h>
#include <m/threadpool/threadpool.h>
#include <m/utility/pointers.h>
#include <m/win32/threadpool.h>

namespace m::filesystem_impl::platform_specific
{
    namespace details
    {
        using file_time = std::chrono::duration<uint64_t, std::ratio<1, 100'000'000'000>>;

    } // namespace details

    class directory_watcher;

    struct registration_token : public m::filesystem::change_notification_registration_token
    {
    public:
        registration_token(uintmax_t key, m::not_null<directory_watcher*> ptr);
        ~registration_token();

        uintmax_t                       m_key;
        m::not_null<directory_watcher*> m_directory_watcher;
    };

    class directory_watcher
    {
    public:
        directory_watcher(std::filesystem::path const& path);

        ~directory_watcher();

        std::unique_ptr<m::filesystem::change_notification_registration_token>
        add_file_watch(uintmax_t                                        key,
                       std::filesystem::path const&                     p,
                       m::not_null<m::filesystem::change_notification*> change_notification_ptr);

        void
        remove_watch(uintmax_t key);

        void
        ensure_watching();

    protected:
        void
        on_directory_probe_timer();

        void
        enqueue_async_read_directory_changes(time_point issue_time);

        void
        invalidate_watcher(time_point issue_time);

        void
        recheck_watcher(time_point issue_time);

        static void
        read_directory_changes_ex_callback(PTP_CALLBACK_INSTANCE CallbackInstance,
                                           PVOID                 Context,
                                           PVOID                 Overlapped,
                                           ULONG                 IoResult,
                                           ULONG_PTR             NumberOfBytesTransferred,
                                           PTP_IO                Io);

        void
        on_read_directory_changes_ex_completed(PTP_CALLBACK_INSTANCE CallbackInstance,
                                               ULONG                 IoResult,
                                               ULONG_PTR             NumberOfBytesTransferred,
                                               PTP_IO                Io);

        // Define a type that we will use for the buffer that the
        // ReadDirectoryChanges data will write into.
        //
        // The std::array<> branch is present only to ensure a certain size.
        //
        union file_notify_buffer
        {
            FILE_NOTIFY_EXTENDED_INFORMATION m_fnei;
            FILE_NOTIFY_INFORMATION          m_fni;
            std::array<std::byte, 32768>     m_data;
        };

        struct file_watch
        {
            file_watch(uintmax_t                                        key,
                       std::filesystem::path const&                     name,
                       m::not_null<m::filesystem::change_notification*> change_notification_ptr):
                m_key(key),
                m_name(name),
                m_name_string(name.c_str()),
                m_name_view(m_name_string),
                m_name_char_count(m::to<int>(m_name_view.size())),
                m_change_notification(change_notification_ptr)
            {}

            uintmax_t             m_key;
            std::filesystem::path m_name;
            std::wstring          m_name_string;
            std::wstring_view     m_name_view;
            int                   m_name_char_count; // Win32 APIs take ints for varied reasons
            m::not_null<m::filesystem::change_notification*> m_change_notification;
        };

        /// <summary>
        /// Compare leaf file names for equality in the way the Windows likes.
        /// </summary>
        /// <param name="pcwstr_lhs">Pointer to first character of first string to compare.</param>
        /// <param name="cch_lhs">Count of characters in the first string to compare. Integer
        /// because the Windows APIs tend to pass things as ints.</param>
        /// <param name="pcwstr_rhs">Pointer to first character of second string to compare.</param>
        /// <param name="cch_rhs">Count of characters in the second string to compare. Integer
        /// because the Windows APIs tend to pass things as ints.</param> <returns>`true` if the
        /// strings are equal, `false` if they are not.</returns>
        static bool
        are_file_names_equal(PCWSTR pcwstr_lhs, int cch_lhs, PCWSTR pcwstr_rhs, int cch_rhs);

        std::mutex                              m_mutex;
        std::chrono::milliseconds               m_default_retry_delay;
        std::chrono::milliseconds               m_minimum_retry_delay;
        std::filesystem::path                   m_path;
        OVERLAPPED                              m_overlapped;
        win32::handle                           m_directory;
        win32::threadpool::tp_io                m_io;
        std::weak_ptr<m::filesystem::monitor>   m_monitor;
        std::vector<file_watch>                 m_registered_watches;
        READ_DIRECTORY_NOTIFY_INFORMATION_CLASS m_information_class;
        bool                                    m_is_valid;
        file_notify_buffer                      m_buffer;
        std::unique_ptr<m::timer>               m_directory_probe_timer;
    };
} // namespace m::filesystem_impl::platform_specific

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#define WIL_ENABLE_EXCEPTIONS

#include <algorithm>
#include <chrono>
#include <format>
#include <stdexcept>
#include <string_view>

using namespace std::chrono_literals;

#include <Windows.h>
#include <bcrypt.h>

#include <m/debugging/dbg_format.h>
#include <m/errors/errors.h>
#include <m/formatters/FILE_NOTIFY_EXTENDED_INFORMATION.h>
#include <m/formatters/FILE_NOTIFY_INFORMATION.h>
#include <m/formatters/OVERLAPPED.h>
#include <m/formatters/Win32ErrorCode.h>
#include <m/strings/convert.h>
#include <m/threadpool/threadpool.h>
#include <m/utility/pointers.h>

#include "directory_watcher.h"

#undef min
#undef max

namespace m::filesystem_impl::platform_specific
{
    template <typename Rep, typename Period>
    static DWORD
    duration_to_win32(std::chrono::duration<Rep, Period> const& duration)
    {
        if (duration.count() < 0)
            throw std::invalid_argument("duration");

        return static_cast<DWORD>(
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    }

    template <typename Rep, typename Period>
    static FILETIME
    duration_to_FILETIME(std::chrono::duration<Rep, Period> const& duration)
    {
        if (duration.count() < 0)
            throw std::invalid_argument("duration");

        union LocalUnion
        {
            int64_t  rep;
            FILETIME file_time;
        };

        static_assert(sizeof(LocalUnion) == sizeof(int64_t));
        static_assert(sizeof(LocalUnion) == sizeof(FILETIME));

        //
        // FILETIME is 100ns units, so for the ratio
        // have the denominator be 1 billion divided by
        // 100.
        //
        using FiletimeRatio    = std::ratio<1, 1'000'000'000 / 100>;
        using FiletimeDuration = std::chrono::duration<int64_t, FiletimeRatio>;

        //
        // Initialization of a union implicitly initializes the first member
        //
        LocalUnion u{std::chrono::duration_cast<FiletimeDuration>(duration).count()};

        // Durations in FILETIME are negative.
        //
        u.rep = -u.rep;

        return u.file_time;
    }

    bool
    directory_watcher::are_file_names_equal(PCWSTR pcwstrLhs,
                                            int    cchLhs,
                                            PCWSTR pcwstrRhs,
                                            int    cchRhs)
    {
        return ((cchLhs == cchRhs) &&
                (::CompareStringOrdinal(pcwstrLhs, cchLhs, pcwstrRhs, cchRhs, TRUE) == CSTR_EQUAL));
    }

    directory_watcher::directory_watcher(std::filesystem::path const& path):
        m_minimum_retry_delay(50ms), m_default_retry_delay(500ms), m_path(path), m_is_valid(true)
    {
        auto l        = [this]() { this->on_directory_probe_timer(); };
        auto path_str = m::to_wstring(path.native());
        auto sp       = m::threadpool->create_timer(l, L"\"{}\" directory probe", path_str);

        using std::swap;
        swap(sp, m_directory_probe_timer);
    }

    void
    directory_watcher::on_directory_probe_timer()
    {
        //
        // The directory probe timer is where we attempt to access the
        // directory and if we cannot, ask the client what to do next.
        //
        auto l = std::unique_lock(m_mutex);

        // If everything looks healthy, there's no reason to be here.
        if (m_directory)
            return;

        m_directory.reset(::CreateFileW(m_path.c_str(),
                                        FILE_LIST_DIRECTORY,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr,
                                        OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
                                        nullptr));
        if (m_directory.get() == INVALID_HANDLE_VALUE)
        {
            auto const last_error = ::GetLastError();

            // Go to each registered watch and see what they have to say about retrying.
            std::chrono::milliseconds         retry_duration = std::chrono::milliseconds::max();
            std::filesystem::filesystem_error error(
                "Unable to open directory"s, m_path, m::make_win32_error_code(last_error));

            for (auto&& rw: m_registered_watches)
            {
                auto r = rw.m_change_notification->on_directory_access_failure(m_path, error);
                auto ms =
                    r.value_or(m::filesystem::change_notification::requeue_directory_access_attempt{
                                   m_default_retry_delay})
                        .m_milliseconds;

                if (ms < retry_duration)
                    retry_duration = ms;
            }

            // Don't let retry_duration be less than the minimum.
            retry_duration = std::max(retry_duration, m_minimum_retry_delay);

            m_directory_probe_timer->set(retry_duration);
        }

        m_io.reset(::CreateThreadpoolIo(
            m_directory.get(), &read_directory_changes_ex_callback, this, nullptr));
        if (m_io.get() == nullptr)
        {
            auto const last_error = ::GetLastError();

            // Close the directory so we don't waste anyone's time.
            m_directory.reset();

            // Go to each registered watch and see what they have to say about retrying.
            std::chrono::milliseconds         retry_duration = std::chrono::milliseconds::max();
            std::filesystem::filesystem_error error(
                "Unable to associate asynchronous I/O with directory"s,
                m_path,
                m::make_win32_error_code(last_error));

            for (auto&& rw: m_registered_watches)
            {
                auto r = rw.m_change_notification->on_directory_access_failure(m_path, error);
                auto ms =
                    r.value_or(m::filesystem::change_notification::requeue_directory_access_attempt{
                                   m_default_retry_delay})
                        .m_milliseconds;

                if (ms < retry_duration)
                    retry_duration = ms;
            }

            // Don't let retry_duration be less than the minimum.
            retry_duration = std::max(retry_duration, m_minimum_retry_delay);

            m_directory_probe_timer->set(retry_duration);
        }

        // TODO: Make the two error paths above common.

        enqueue_async_read_directory_changes();
    }

    std::unique_ptr<m::filesystem::change_notification_registration_token>
    directory_watcher::add_file_watch(uintmax_t                                        key,
                                      std::filesystem::path const&                     p,
                                      m::not_null<m::filesystem::change_notification*> ptr)
    {
        auto filename = p.filename();

        if (p.has_parent_path())
            throw std::runtime_error("path must be leaf only");

        auto l = std::unique_lock(m_mutex);

        auto token = std::make_unique<registration_token>(key, m::not_null(this));

        m_registered_watches.emplace_back(key, filename, ptr);

        ptr->on_begin();

        return token;
    }

    void
    directory_watcher::ensure_watching()
    {
        auto l = std::unique_lock(m_mutex);
        if (!m_directory)
            m_directory_probe_timer->set(std::chrono::milliseconds(0));
    }

    void
    directory_watcher::remove_watch(uintmax_t key)
    {
        auto l = std::unique_lock(m_mutex);

        auto result = std::ranges::find_if(m_registered_watches,
                                           [key](auto const& w) { return w.m_key == key; });
        if (result == m_registered_watches.end())
            throw std::runtime_error("No matching watch found for key");

        result->m_change_notification->on_cancelled();

        m_registered_watches.erase(result);
    }

    void
    directory_watcher::recheck_watcher()
    {
        auto s  = m_path.native();
        auto sv = std::wstring_view(s);

        // m::dbg_format(L"directory_watcher::RecheckWatcher() for path {}\n", sv);
        auto str = std::format(L"directory_watcher::rechec_wWatcher() for path {}\n", sv);

        for (auto&& e: m_registered_watches)
            e.m_change_notification->on_file_recheck_required(m_path, e.m_name);
    }

    void
    directory_watcher::invalidate_watcher()
    {
        auto s = m_path.native();
        m::dbg_format(L"directory_watcher::InvalidateWatcher() for path {}\n", s);

        for (auto&& e: m_registered_watches)
        {
            e.m_change_notification->on_invalid();
        }

        m_is_valid = false;
    }

    void
    directory_watcher::enqueue_async_read_directory_changes()
    {
        m_overlapped.hEvent       = nullptr;
        m_overlapped.Internal     = 0;
        m_overlapped.InternalHigh = 0;
        m_overlapped.Pointer      = 0;

        ::StartThreadpoolIo(m_io.get());

        if (::ReadDirectoryChangesExW(
                m_directory.get(),
                &m_buffer,
                sizeof(m_buffer),
                FALSE, // bWatchSubtree
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                    FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, // lpBytesReturned - undefined since we are asynchronous
                &m_overlapped,
                nullptr, // lpCompletionRoutine - nullptr because we are using threadpool
                ReadDirectoryNotifyExtendedInformation))
        {
            m_information_class = ReadDirectoryNotifyExtendedInformation;
            return;
        }

        auto last_error = ::GetLastError();

        ::CancelThreadpoolIo(m_io.get());

        m::dbg_format(L"ReadDirectoryChangesExW() failed with Win32 error code {}\n",
                      fmtWin32ErrorCode{last_error});

        if (last_error == ERROR_NOTIFY_ENUM_DIR)
        {
            recheck_watcher();
            return;
        }

        if (last_error == ERROR_ACCESS_DENIED)
        {
            // ERROR_ACCESS_DENIED happens when the directory is deleted.
            invalidate_watcher();
            return;
        }

        if (last_error != ERROR_INVALID_FUNCTION)
            m::throw_win32_error_code(last_error);

        // ReFS doesn't seem to support the extended information format, so
        // try using the non-extended format.

        m_overlapped.hEvent       = nullptr;
        m_overlapped.Internal     = 0;
        m_overlapped.InternalHigh = 0;
        m_overlapped.Pointer      = 0;

        StartThreadpoolIo(m_io.get());

        if (::ReadDirectoryChangesW(
                m_directory.get(),
                &m_buffer,
                sizeof(m_buffer),
                FALSE, // bWatchSubtree
                FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                    FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE,
                nullptr, // lpBytesReturned - undefined since we are asynchronous
                &m_overlapped,
                nullptr)) // lpCompletionRoutine - nullptr because we are using threadpool
        {
            m_information_class = ReadDirectoryNotifyInformation;
            return;
        }

        last_error = ::GetLastError();

        m::dbg_format(L"ReadDirectoryChangesW() failed with Win32 error code {}\n",
                      fmtWin32ErrorCode{last_error});

        ::CancelThreadpoolIo(m_io.get());

        if (last_error == ERROR_NOTIFY_ENUM_DIR)
        {
            recheck_watcher();
            return;
        }

        if (last_error == ERROR_ACCESS_DENIED)
        {
            // ERROR_ACCESS_DENIED happens when the directory is deleted.
            invalidate_watcher();
            return;
        }

        m::throw_win32_error_code(last_error);
    }

    void
    directory_watcher::read_directory_changes_ex_callback(PTP_CALLBACK_INSTANCE CallbackInstance,
                                                          PVOID                 Context,
                                                          PVOID                 Overlapped,
                                                          ULONG                 IoResult,
                                                          ULONG_PTR NumberOfBytesTransferred,
                                                          PTP_IO    Io)
    {
#if 0
        m::dbg_format(
            L"directory_watcher::ReadDirectoryChangesExCallback(CallbackInstance = 0x{:x}, Context = 0x{:x}, Overlapped = {}, IoResult = {}, NumberOfBytesTransferred = {}, Io = 0x{:x})\n",
            reinterpret_cast<uintptr_t>(CallbackInstance),
            reinterpret_cast<uintptr_t>(Context),
            *reinterpret_cast<OVERLAPPED*>(Overlapped),
            fmtWin32ErrorCode{IoResult},
            NumberOfBytesTransferred,
            reinterpret_cast<uintptr_t>(Io));
#else
        std::ignore = Overlapped;
#endif

        //
        // Thunk over to member function for easier reading
        //
        reinterpret_cast<directory_watcher*>(Context)->on_read_directory_changes_ex_completed(
            CallbackInstance, IoResult, NumberOfBytesTransferred, Io);
    }

    void
    directory_watcher::on_read_directory_changes_ex_completed(
        PTP_CALLBACK_INSTANCE CallbackInstance,
        ULONG                 IoResult,
        ULONG_PTR             NumberOfBytesTransferred,
        PTP_IO                Io)
    {
        std::ignore = CallbackInstance;
        std::ignore = IoResult;
        std::ignore = NumberOfBytesTransferred;
        std::ignore = Io;

        auto l = std::unique_lock(m_mutex);

        if (IoResult != ERROR_SUCCESS)
        {
            invalidate_watcher();
            return;
        }

        switch (m_information_class)
        {
            default:

            case ReadDirectoryNotifyInformation:
            {
                auto fni = &m_buffer.m_fni;

                for (;;)
                {
                    // m::dbg_format(L"Processing file change: {}\n", *fni);
                    int cchFileName = fni->FileNameLength / sizeof(fni->FileName[0]);

                    // only make this copy if we have a match
                    std::filesystem::path asPath{};

                    for (auto&& e: m_registered_watches)
                    {
#if 0
                        m::dbg_format(L"Comparing \"{}\" and \"{}\"\n",
                                      std::wstring_view(e.m_name_view.data(), e.m_name_char_count),
                                      std::wstring_view(fni->FileName, cchFileName));
#endif
                        if (are_file_names_equal(e.m_name_view.data(),
                                                 e.m_name_char_count,
                                                 fni->FileName,
                                                 cchFileName))
                        {
                            if (asPath.empty())
                                asPath = std::wstring_view(
                                    fni->FileName, fni->FileNameLength / sizeof(fni->FileName[0]));

                            switch (fni->Action)
                            {
                                case FILE_ACTION_ADDED:
                                case FILE_ACTION_MODIFIED:
                                case FILE_ACTION_RENAMED_NEW_NAME:
                                    e.m_change_notification->on_file_changed(m_path, asPath);
                                    break;

                                case FILE_ACTION_REMOVED:
                                case FILE_ACTION_RENAMED_OLD_NAME:
                                    e.m_change_notification->on_file_deleted(m_path, asPath);
                                    break;
                            }
                        }
                    }

                    if (fni->NextEntryOffset == 0)
                        break;

                    fni = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(
                        reinterpret_cast<uintptr_t>(fni) + fni->NextEntryOffset);
                }

                break;
            }

            case ReadDirectoryNotifyExtendedInformation:
            {
                auto fnei = &m_buffer.m_fnei;

                for (;;)
                {
                    // m::dbg_format(L"Processing file change: {}\n", *fnei);

                    int cchFileName = fnei->FileNameLength / sizeof(fnei->FileName[0]);
                    // only make this copy if we have a match
                    std::filesystem::path asPath{};

                    for (auto&& e: m_registered_watches)
                    {
#if 0
                        m::dbg_format(L"Comparing \"{}\" and \"{}\"\n",
                                      std::wstring_view(e.m_name_view.data(), e.m_name_char_count),
                                      std::wstring_view(fnei->FileName, cchFileName));
#endif

                        if (are_file_names_equal(e.m_name_view.data(),
                                                 e.m_name_char_count,
                                                 fnei->FileName,
                                                 cchFileName))
                        {
                            if (asPath.empty())
                                asPath = std::wstring_view(fnei->FileName,
                                                           fnei->FileNameLength /
                                                               sizeof(fnei->FileName[0]));

                            switch (fnei->Action)
                            {
                                case FILE_ACTION_ADDED:
                                case FILE_ACTION_MODIFIED:
                                case FILE_ACTION_RENAMED_NEW_NAME:
                                    e.m_change_notification->on_file_changed(m_path, asPath);
                                    break;

                                case FILE_ACTION_REMOVED:
                                case FILE_ACTION_RENAMED_OLD_NAME:
                                    e.m_change_notification->on_file_deleted(m_path, asPath);
                                    break;
                            }
                        }
                    }

                    if (fnei->NextEntryOffset == 0)
                        break;

                    fnei = reinterpret_cast<FILE_NOTIFY_EXTENDED_INFORMATION*>(
                        reinterpret_cast<uintptr_t>(fnei) + fnei->NextEntryOffset);
                }

                break;
            }
        }

        enqueue_async_read_directory_changes();
    }

    registration_token::registration_token(uintmax_t key, m::not_null<directory_watcher*> ptr):
        m_key(key), m_directory_watcher(ptr)
    {}

    registration_token::~registration_token() { m_directory_watcher->remove_watch(m_key); }

} // namespace m::filesystem_impl::platform_specific

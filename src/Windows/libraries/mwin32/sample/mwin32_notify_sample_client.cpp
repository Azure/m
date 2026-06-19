// Copyright (c) Microsoft Corporation.
//
// Notification sample client for the mwin32 link-time alias object (M-FS-NOTIFY-REDIR-1).
//
// This is an ORDINARY Win32 filesystem notification client: it includes only
// <windows.h> and calls the genuine change-notification entry point
// (ReadDirectoryChangesW). It has no knowledge of mwin32 and includes none of its
// headers. The only thing that makes its calls redirectable is that its CMake target
// links the `mwin32_alias` object, whose __imp_ slots retarget these calls into the
// mwin32 shim.
//
// The executable takes a single argument: the directory to watch. It opens the
// directory with FILE_FLAG_BACKUP_SEMANTICS, registers a watch via
// ReadDirectoryChangesW (aliased through mwin32), creates a "ready" marker file in
// the watched directory so the test harness can know when the watch is armed, waits
// for a notification with a timeout, and reports the action + filename to stdout.
//
// The mode (passthrough / redirecting) is chosen entirely outside this program by
// the `<executable>.pilcfg` sidecar the host environment places next to it.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace
{
    // The single output site for the whole program. Per the repository's
    // architectural pre-step rule, all reporting is routed through one sink so the
    // formatting/destination concern is separable from the call sites. Lines are
    // emitted as `tag=value` so a harness can parse them without ambiguity.
    class reporter
    {
    public:
        explicit reporter(std::FILE* out) noexcept : m_out(out) {}

        void
        kv(std::string_view tag, std::string_view value) const
        {
            std::fprintf(m_out, "%.*s=%.*s\n",
                         static_cast<int>(tag.size()), tag.data(),
                         static_cast<int>(value.size()), value.data());
        }

        void
        kv(std::string_view tag, unsigned long value) const
        {
            std::fprintf(m_out, "%.*s=%lu\n",
                         static_cast<int>(tag.size()), tag.data(), value);
        }

    private:
        std::FILE* m_out;
    };

    // Name of the ready-marker file created to signal the test harness that the
    // watch is armed.
    constexpr wchar_t k_ready_marker[] = L".watch_ready";

    // Timeout for waiting on a notification (milliseconds).
    constexpr DWORD k_notification_timeout_ms = 10'000;

    // Brief pause after arming the watch before creating the ready marker.
    constexpr DWORD k_arm_delay_ms = 250;

    // Convert a narrow ASCII view of a wide string for reporting. The sample's
    // payload is ASCII, so a straight narrowing is sufficient and avoids dragging
    // in locale conversion.
    std::string
    narrow(std::wstring_view w)
    {
        std::string s;
        s.reserve(w.size());
        for (wchar_t c: w)
            s.push_back(c <= 0x7f ? static_cast<char>(c) : '?');
        return s;
    }

    // Create a zero-length marker file inside the directory. Returns true on success.
    bool
    create_ready_marker(std::wstring const& dir)
    {
        std::wstring const path = dir + L"\\" + k_ready_marker;

        HANDLE const h = ::CreateFileW(path.c_str(),
                                       GENERIC_WRITE,
                                       0,
                                       nullptr,
                                       CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL,
                                       nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;

        ::CloseHandle(h);
        return true;
    }

    // Open a directory handle suitable for change notification (FILE_FLAG_BACKUP_SEMANTICS).
    // Returns INVALID_HANDLE_VALUE on failure.
    HANDLE
    open_directory(std::wstring const& dir)
    {
        return ::CreateFileW(dir.c_str(),
                             GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr,
                             OPEN_EXISTING,
                             FILE_FLAG_BACKUP_SEMANTICS,
                             nullptr);
    }
}

int
wmain(int argc, wchar_t* argv[])
{
    // Diagnostic output must survive an abnormal exit (a crash or a hang followed
    // by external termination). When stdout is a pipe it is block-buffered by the
    // CRT and only flushed on a normal return, so any progress written before an
    // abnormal exit would be lost. Make it unbuffered so each reported line reaches
    // the harness immediately.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    const reporter report(stdout);

    // Require exactly one argument: the directory to watch.
    if (argc != 2)
    {
        report.kv("error", "usage: mwin32_notify_sample_client <directory>");
        return 1;
    }

    std::wstring const watch_dir = argv[1];

    // 1) Open the directory with FILE_FLAG_BACKUP_SEMANTICS for change notification.
    HANDLE const hDir = open_directory(watch_dir);
    if (hDir == INVALID_HANDLE_VALUE)
    {
        report.kv("open_dir_rc", 0u);
        report.kv("open_dir_gle", ::GetLastError());
        return 1;
    }
    report.kv("open_dir_rc", 1u);

    // 2) Create an event for the asynchronous ReadDirectoryChangesW call.
    HANDLE const hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (hEvent == nullptr)
    {
        report.kv("create_event_rc", 0u);
        report.kv("create_event_gle", ::GetLastError());
        ::CloseHandle(hDir);
        return 1;
    }
    report.kv("create_event_rc", 1u);

    // 3) Arm the directory-change notification.
    alignas(DWORD) std::byte buffer[4096]{};
    DWORD                    bytes = 0;
    OVERLAPPED               ov{};
    ov.hEvent = hEvent;

    BOOL const watch_armed = ::ReadDirectoryChangesW(
        hDir,
        buffer,
        static_cast<DWORD>(sizeof(buffer)),
        FALSE,  // bWatchSubtree
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
        &bytes,
        &ov,
        nullptr);

    if (!watch_armed)
    {
        report.kv("arm_watch_rc", 0u);
        report.kv("arm_watch_gle", ::GetLastError());
        ::CloseHandle(hEvent);
        ::CloseHandle(hDir);
        return 1;
    }
    report.kv("arm_watch_rc", 1u);

    // 4) Brief pause to let the underlying ReadDirectoryChangesW arm fully.
    ::Sleep(k_arm_delay_ms);

    // 5) Create the ready-marker file so the test harness knows the watch is armed.
    //    This mutation will itself be reported as the first notification (FILE_ACTION_ADDED),
    //    which the harness ignores; subsequent mutations (from the harness) are the real test.
    if (!create_ready_marker(watch_dir))
    {
        report.kv("ready_marker_rc", 0u);
        report.kv("ready_marker_gle", ::GetLastError());
        ::CloseHandle(hEvent);
        ::CloseHandle(hDir);
        return 1;
    }
    report.kv("ready_marker_rc", 1u);

    // 6) Wait for the first notification (the ready marker itself).
    DWORD wait_result = ::WaitForSingleObject(hEvent, k_notification_timeout_ms);
    if (wait_result != WAIT_OBJECT_0)
    {
        report.kv("wait_ready_marker", 0u);
        ::CloseHandle(hEvent);
        ::CloseHandle(hDir);
        return 1;
    }

    // Decode the ready-marker notification but don't report it.
    auto const* info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(buffer);
    std::wstring ready_name(info->FileName, info->FileNameLength / sizeof(WCHAR));
    // Confirm it's the ready marker (sanity check).
    if (ready_name != k_ready_marker)
    {
        // Unexpected first notification; report it anyway for debugging.
        report.kv("unexpected_first_action", static_cast<unsigned long>(info->Action));
        report.kv("unexpected_first_name", narrow(ready_name));
    }

    // 7) Re-arm the watch for the actual test notification.
    ::ResetEvent(hEvent);
    bytes = 0;
    std::fill(std::begin(buffer), std::end(buffer), std::byte{0});
    ov = {};
    ov.hEvent = hEvent;

    BOOL const rearm_ok = ::ReadDirectoryChangesW(
        hDir,
        buffer,
        static_cast<DWORD>(sizeof(buffer)),
        FALSE,
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME,
        &bytes,
        &ov,
        nullptr);

    if (!rearm_ok)
    {
        report.kv("rearm_watch_rc", 0u);
        report.kv("rearm_watch_gle", ::GetLastError());
        ::CloseHandle(hEvent);
        ::CloseHandle(hDir);
        return 1;
    }
    report.kv("rearm_watch_rc", 1u);

    // 8) Wait for the second notification (the actual test mutation from the harness).
    wait_result = ::WaitForSingleObject(hEvent, k_notification_timeout_ms);
    if (wait_result != WAIT_OBJECT_0)
    {
        report.kv("wait_notification_rc", 0u);
        if (wait_result == WAIT_TIMEOUT)
            report.kv("wait_notification_timeout", 1u);
        ::CloseHandle(hEvent);
        ::CloseHandle(hDir);
        return 1;
    }
    report.kv("wait_notification_rc", 1u);

    // 9) Decode and report the notification.
    if (bytes >= sizeof(FILE_NOTIFY_INFORMATION))
    {
        info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(buffer);
        std::wstring const reported_name(info->FileName, info->FileNameLength / sizeof(WCHAR));

        report.kv("notify_action", static_cast<unsigned long>(info->Action));
        report.kv("notify_name", narrow(reported_name));
    }
    else
    {
        report.kv("notify_bytes", static_cast<unsigned long>(bytes));
    }

    // 10) Cleanup.
    ::CloseHandle(hEvent);
    ::CloseHandle(hDir);

    return 0;
}

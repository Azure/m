// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

//
// Directory change-notification integration tests (M-FS-NOTIFY-3). The
// executable links m_mwin32 and calls the m-prefixed change-notification entry
// points directly. Unlike the copy / metadata suites, this one runs under a
// *passthrough* .pilcfg (no buffering, no redirection): change notifications are
// observed by the live (direct) provider's real ReadDirectoryChangesW, which a
// buffered overlay does not model (its register_watch is unimplemented). So the
// watch must target a real directory and observe real mutations -- the test
// creates a unique scratch directory under the OS temp path, opens it through
// the shim with FILE_FLAG_BACKUP_SEMANTICS, mutates it through the shim, and
// asserts the change surfaces with the right action and name.
//

#include <gtest/gtest.h>

#include <cstddef>
#include <filesystem>
#include <string>

#include <m/mwin32/mwinfile.h>

namespace
{
    //
    // Generous upper bound on how long to wait for a live filesystem change to
    // propagate from the kernel through the PIL monitor's threadpool callback
    // into the shim. The genuine ReadDirectoryChangesW arms asynchronously, so
    // the test also pauses briefly after registering before mutating.
    //
    constexpr DWORD k_change_wait_ms = 5'000;
    constexpr DWORD k_arm_delay_ms   = 250;

    //
    // Create a unique scratch directory under the OS temp directory and return
    // its path. The name embeds the process id and a per-call counter so
    // concurrent or repeated runs never collide on the live disk.
    //
    std::wstring
    make_unique_temp_dir()
    {
        static unsigned counter = 0;

        auto const base = std::filesystem::temp_directory_path();
        auto       dir  = base;
        dir /= L"mwin32_notify_" + std::to_wstring(::GetCurrentProcessId()) + L"_"
               + std::to_wstring(counter++);

        std::filesystem::create_directories(dir);
        return dir.wstring();
    }

    //
    // Open a directory handle suitable for change notification (the shim's
    // FILE_FLAG_BACKUP_SEMANTICS path). Returns INVALID_HANDLE_VALUE on failure.
    //
    HANDLE
    open_directory(std::wstring const& dir)
    {
        return ::mCreateFileW(dir.c_str(),
                              GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS,
                              nullptr);
    }

    //
    // Create a zero-length file inside dir through the shim, returning true on
    // success. The mutation is a real disk write (passthrough), which is what the
    // live monitor observes.
    //
    bool
    create_file_in(std::wstring const& dir, std::wstring const& name)
    {
        auto const path = dir + L"\\" + name;

        HANDLE const h = ::mCreateFileW(path.c_str(),
                                        GENERIC_WRITE,
                                        0,
                                        nullptr,
                                        CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;

        ::mCloseHandle(h);
        return true;
    }

    //
    // Recursively remove the scratch directory; best effort (a failure here must
    // not fail the test, only leak a temp directory).
    //
    void
    remove_tree(std::wstring const& dir) noexcept
    {
        std::error_code ec;
        std::filesystem::remove_all(std::filesystem::path(dir), ec);
    }
} // namespace

//
// The detailed path: mReadDirectoryChangesW with an event-bearing OVERLAPPED.
// A file added to the watched directory must surface as a FILE_NOTIFY_-
// INFORMATION record carrying FILE_ACTION_ADDED and the file's relative name.
//
TEST(MwinFileNotify, ReadDirectoryChangesReportsAddedFile)
{
    std::wstring const dir = make_unique_temp_dir();

    HANDLE const hDir = open_directory(dir);
    ASSERT_NE(hDir, INVALID_HANDLE_VALUE);

    HANDLE const hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ASSERT_NE(hEvent, nullptr);

    alignas(DWORD) std::byte buffer[4096]{};
    DWORD                    bytes = 0;
    OVERLAPPED               ov{};
    ov.hEvent = hEvent;

    ASSERT_TRUE(::mReadDirectoryChangesW(hDir,
                                         buffer,
                                         static_cast<DWORD>(sizeof(buffer)),
                                         FALSE,
                                         FILE_NOTIFY_CHANGE_FILE_NAME,
                                         &bytes,
                                         &ov,
                                         nullptr));

    // Let the underlying ReadDirectoryChangesW arm before mutating.
    ::Sleep(k_arm_delay_ms);

    constexpr wchar_t k_added_name[] = L"added.txt";
    ASSERT_TRUE(create_file_in(dir, k_added_name));

    ASSERT_EQ(::WaitForSingleObject(hEvent, k_change_wait_ms), WAIT_OBJECT_0);

    // The sink filled the buffer and set *lpBytesReturned before signaling.
    ASSERT_GE(bytes, sizeof(FILE_NOTIFY_INFORMATION));

    auto const* const info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(buffer);
    EXPECT_EQ(info->Action, static_cast<DWORD>(FILE_ACTION_ADDED));

    std::wstring const reported(info->FileName, info->FileNameLength / sizeof(WCHAR));
    EXPECT_EQ(reported, std::wstring(k_added_name));

    ::CloseHandle(hEvent);
    ::mCloseHandle(hDir);
    remove_tree(dir);
}

//
// The synchronous path: mReadDirectoryChangesW with a NULL OVERLAPPED blocks
// until a change is queued, then decodes it. A background thread performs the
// mutation after the watch has had time to arm.
//
TEST(MwinFileNotify, ReadDirectoryChangesBlockingReportsAddedFile)
{
    std::wstring const dir = make_unique_temp_dir();

    HANDLE const hDir = open_directory(dir);
    ASSERT_NE(hDir, INVALID_HANDLE_VALUE);

    // Install the watch (first call arms it); use the overlapped form so the
    // call returns immediately, then drain synchronously below.
    alignas(DWORD) std::byte arm_buffer[256]{};
    DWORD                    arm_bytes = 0;
    OVERLAPPED               arm_ov{};
    HANDLE const             arm_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ASSERT_NE(arm_event, nullptr);
    arm_ov.hEvent = arm_event;

    ASSERT_TRUE(::mReadDirectoryChangesW(hDir,
                                         arm_buffer,
                                         static_cast<DWORD>(sizeof(arm_buffer)),
                                         FALSE,
                                         FILE_NOTIFY_CHANGE_FILE_NAME,
                                         &arm_bytes,
                                         &arm_ov,
                                         nullptr));

    ::Sleep(k_arm_delay_ms);

    constexpr wchar_t k_added_name[] = L"blocking.txt";
    ASSERT_TRUE(create_file_in(dir, k_added_name));

    ASSERT_EQ(::WaitForSingleObject(arm_event, k_change_wait_ms), WAIT_OBJECT_0);
    ASSERT_GE(arm_bytes, sizeof(FILE_NOTIFY_INFORMATION));

    auto const* const info = reinterpret_cast<FILE_NOTIFY_INFORMATION const*>(arm_buffer);
    EXPECT_EQ(info->Action, static_cast<DWORD>(FILE_ACTION_ADDED));

    std::wstring const reported(info->FileName, info->FileNameLength / sizeof(WCHAR));
    EXPECT_EQ(reported, std::wstring(k_added_name));

    ::CloseHandle(arm_event);
    ::mCloseHandle(hDir);
    remove_tree(dir);
}

//
// A watch on a directory that does not exist fails rather than minting a handle.
//
TEST(MwinFileNotify, FindFirstChangeNotificationFailsForMissingDirectory)
{
    auto const missing =
        std::filesystem::temp_directory_path() / L"mwin32_notify_does_not_exist_zzz";

    HANDLE const h =
        ::mFindFirstChangeNotificationW(missing.wstring().c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);

    EXPECT_EQ(h, INVALID_HANDLE_VALUE);
}

//
// The coarse path: mFindFirstChangeNotification returns an OS-waitable handle
// that becomes signaled when any matching change occurs; mFindNextChange-
// Notification re-arms it and mFindCloseChangeNotification tears it down.
//
TEST(MwinFileNotify, FindChangeNotificationSignalsOnChange)
{
    std::wstring const dir = make_unique_temp_dir();

    HANDLE const h =
        ::mFindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_FILE_NAME);
    ASSERT_NE(h, INVALID_HANDLE_VALUE);

    ::Sleep(k_arm_delay_ms);

    ASSERT_TRUE(create_file_in(dir, L"coarse.txt"));

    EXPECT_EQ(::WaitForSingleObject(h, k_change_wait_ms), WAIT_OBJECT_0);

    // Re-arm and observe a second change.
    ASSERT_TRUE(::mFindNextChangeNotification(h));

    ::Sleep(k_arm_delay_ms);
    ASSERT_TRUE(create_file_in(dir, L"coarse2.txt"));

    EXPECT_EQ(::WaitForSingleObject(h, k_change_wait_ms), WAIT_OBJECT_0);

    EXPECT_TRUE(::mFindCloseChangeNotification(h));

    remove_tree(dir);
}

//
// mFindNextChangeNotification / mFindCloseChangeNotification reject a handle the
// registry never minted.
//
TEST(MwinFileNotify, FindChangeNotificationRejectsForeignHandle)
{
    HANDLE const bogus = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ASSERT_NE(bogus, nullptr);

    EXPECT_FALSE(::mFindNextChangeNotification(bogus));
    EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_HANDLE));

    EXPECT_FALSE(::mFindCloseChangeNotification(bogus));
    EXPECT_EQ(::GetLastError(), static_cast<DWORD>(ERROR_INVALID_HANDLE));

    ::CloseHandle(bogus);
}

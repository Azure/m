// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include <gtest/gtest.h>

#include <Windows.h>

#include <m/pil/file_path.h>
#include <m/pil/filesystem.h>
#include <m/pil/pil.h>

//
// Change-notification integration tests for the live Windows filesystem
// provider (M-FS-MONITOR-1). A temporary directory is watched through the
// direct provider's filesystem_monitor (ReadDirectoryChangesW), then files are
// created / renamed / deleted with std::filesystem (the ground truth) and the
// detailed on_change(...) callbacks are verified by change_kind.
//

namespace
{
    using namespace std::chrono_literals;

    m::pil::file_path
    to_file_path(std::filesystem::path const& p)
    {
        std::wstring const   ws = p.wstring();
        std::u16string const u16(ws.begin(), ws.end());
        return m::pil::file_path(m::pil::file_path::view_type(u16));
    }

    // A unique temporary directory that is removed on destruction.
    class scoped_temp_dir
    {
    public:
        scoped_temp_dir()
        {
            auto const base = std::filesystem::temp_directory_path();
            m_path = base / (L"m_pil_fs_monitor_" + std::to_wstring(::GetCurrentProcessId()) +
                             L"_" + std::to_wstring(s_counter++));
            std::filesystem::remove_all(m_path);
            std::filesystem::create_directories(m_path);
        }

        scoped_temp_dir(scoped_temp_dir const&)            = delete;
        scoped_temp_dir& operator=(scoped_temp_dir const&) = delete;

        ~scoped_temp_dir()
        {
            std::error_code ec;
            std::filesystem::remove_all(m_path, ec);
        }

        std::filesystem::path const&
        path() const noexcept
        {
            return m_path;
        }

    private:
        static inline unsigned s_counter = 0;
        std::filesystem::path  m_path;
    };

    // Records change notifications, tallied per change kind so create / rename /
    // delete can be distinguished.
    struct monitor_sink : public m::pil::ifilesystem_monitor_change_notification
    {
        ~monitor_sink() = default;

        void
        on_begin(m::utc_time_point_type const&) override
        {
            m_on_begins++;
        }

        std::optional<requeue_directory_access_attempt>
        on_directory_access_failure(m::utc_time_point_type const&,
                                    m::pil::file_path const&,
                                    std::system_error const&) override
        {
            m_on_directory_access_failures++;
            return std::nullopt;
        }

        std::optional<requeue_change_notification_attempt>
        on_change_notification_attempt_failure(m::utc_time_point_type const&,
                                               m::pil::file_path const&,
                                               std::system_error const&) override
        {
            m_on_change_notification_attempt_failures++;
            return std::nullopt;
        }

        void
        on_change(m::utc_time_point_type const&,
                  m::pil::file_path const&,
                  m::pil::filesystem_change_kind kind,
                  m::pil::file_path const&       entry_name) override
        {
            using enum m::pil::filesystem_change_kind;

            m_on_changes++;

            switch (kind)
            {
                case added: m_added++; break;
                case removed: m_removed++; break;
                case modified: m_modified++; break;
                case renamed_old_name: m_renamed_old++; break;
                case renamed_new_name: m_renamed_new++; break;
            }

            {
                auto l = std::unique_lock(m_mutex);
                m_last_entry_name = std::u16string(entry_name.native().view());
            }
        }

        void
        on_cancelled(m::utc_time_point_type const&) override
        {
            m_on_cancelleds++;
        }

        std::u16string
        last_entry_name()
        {
            auto l = std::unique_lock(m_mutex);
            return m_last_entry_name;
        }

        std::atomic<uintmax_t> m_on_begins{};
        std::atomic<uintmax_t> m_on_directory_access_failures{};
        std::atomic<uintmax_t> m_on_change_notification_attempt_failures{};
        std::atomic<uintmax_t> m_on_changes{};
        std::atomic<uintmax_t> m_on_cancelleds{};
        std::atomic<uintmax_t> m_added{};
        std::atomic<uintmax_t> m_removed{};
        std::atomic<uintmax_t> m_modified{};
        std::atomic<uintmax_t> m_renamed_old{};
        std::atomic<uintmax_t> m_renamed_new{};

        std::mutex     m_mutex;
        std::u16string m_last_entry_name;
    };

    // Polls until the predicate is satisfied or the timeout elapses. Used so the
    // asynchronous ReadDirectoryChangesW delivery does not make the tests racy.
    template <typename Predicate>
    bool
    wait_until(Predicate&& pred, std::chrono::milliseconds timeout = 3000ms)
    {
        auto const deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (pred())
                return true;
            std::this_thread::sleep_for(20ms);
        }
        return pred();
    }

    m::pil::filesystem_monitor::register_watch_flags
    name_watch_flags()
    {
        using enum m::pil::filesystem_monitor::register_watch_flags;
        return file_name_changes | directory_name_changes;
    }

    TEST(DirectFilesystemMonitoring, ReportsFileCreate)
    {
        scoped_temp_dir const tmp;

        auto fs  = m::pil::make_platform().get_filesystem();
        auto mon = fs.monitor();

        monitor_sink sink;
        auto         token = mon.register_watch(name_watch_flags(), to_file_path(tmp.path()), &sink);

        // Give the watch a moment to arm before mutating.
        std::this_thread::sleep_for(50ms);

        {
            std::ofstream f(tmp.path() / L"created.txt");
            f << "hello";
        }

        EXPECT_TRUE(wait_until([&] { return sink.m_added.load() >= 1; }));
        EXPECT_GE(sink.m_added.load(), 1u);
        EXPECT_EQ(sink.last_entry_name(), u"created.txt");
    }

    TEST(DirectFilesystemMonitoring, ReportsFileDelete)
    {
        scoped_temp_dir const tmp;
        {
            std::ofstream f(tmp.path() / L"victim.txt");
            f << "data";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto mon = fs.monitor();

        monitor_sink sink;
        auto         token = mon.register_watch(name_watch_flags(), to_file_path(tmp.path()), &sink);

        std::this_thread::sleep_for(50ms);

        std::filesystem::remove(tmp.path() / L"victim.txt");

        EXPECT_TRUE(wait_until([&] { return sink.m_removed.load() >= 1; }));
        EXPECT_GE(sink.m_removed.load(), 1u);
    }

    TEST(DirectFilesystemMonitoring, ReportsFileRename)
    {
        scoped_temp_dir const tmp;
        {
            std::ofstream f(tmp.path() / L"before.txt");
            f << "data";
        }

        auto fs  = m::pil::make_platform().get_filesystem();
        auto mon = fs.monitor();

        monitor_sink sink;
        auto         token = mon.register_watch(name_watch_flags(), to_file_path(tmp.path()), &sink);

        std::this_thread::sleep_for(50ms);

        std::filesystem::rename(tmp.path() / L"before.txt", tmp.path() / L"after.txt");

        EXPECT_TRUE(wait_until(
            [&] { return sink.m_renamed_old.load() >= 1 && sink.m_renamed_new.load() >= 1; }));
        EXPECT_GE(sink.m_renamed_old.load(), 1u);
        EXPECT_GE(sink.m_renamed_new.load(), 1u);
    }

    TEST(DirectFilesystemMonitoring, ReportsSubtreeChange)
    {
        scoped_temp_dir const tmp;
        std::filesystem::create_directory(tmp.path() / L"sub");

        auto fs  = m::pil::make_platform().get_filesystem();
        auto mon = fs.monitor();

        monitor_sink sink;
        auto         token =
            mon.register_watch(name_watch_flags() |
                                   m::pil::filesystem_monitor::register_watch_flags::watch_subtree,
                               to_file_path(tmp.path()),
                               &sink);

        std::this_thread::sleep_for(50ms);

        {
            std::ofstream f(tmp.path() / L"sub" / L"nested.txt");
            f << "deep";
        }

        EXPECT_TRUE(wait_until([&] { return sink.m_added.load() >= 1; }));
        EXPECT_GE(sink.m_added.load(), 1u);
    }

    TEST(DirectFilesystemMonitoring, NoNotificationsAfterTokenReset)
    {
        scoped_temp_dir const tmp;

        auto fs  = m::pil::make_platform().get_filesystem();
        auto mon = fs.monitor();

        monitor_sink sink;
        auto         token = mon.register_watch(name_watch_flags(), to_file_path(tmp.path()), &sink);

        std::this_thread::sleep_for(50ms);

        token.reset();

        {
            std::ofstream f(tmp.path() / L"late.txt");
            f << "ignored";
        }

        std::this_thread::sleep_for(100ms);

        EXPECT_EQ(sink.m_added.load(), 0u);
    }
} // namespace

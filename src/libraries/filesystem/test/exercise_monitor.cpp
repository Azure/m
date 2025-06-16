// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <span>
#include <string_view>

#include <m/debugging/dbg_format.h>
#include <m/filesystem/filesystem.h>
#include <m/googletest/temporary_directory.h>

using namespace std::chrono_literals;
using namespace std::string_view_literals;

struct change_notification_sink : public m::filesystem::change_notification
{
    std::atomic<uintmax_t> m_on_begin_count{0};
    std::atomic<uintmax_t> m_on_directory_access_failure_count{0};
    std::atomic<uintmax_t> m_on_file_access_failure_count{0};
    std::atomic<uintmax_t> m_on_file_changed_count{0};
    std::atomic<uintmax_t> m_on_file_deleted_count{0};
    std::atomic<uintmax_t> m_on_file_recheck_required_count{0};
    std::atomic<uintmax_t> m_on_cancelled_count{0};
    std::atomic<uintmax_t> m_on_invalid_count{0};

    void
    on_begin() override
    {
        m::dbg_format("Entering {}", __func__);
        m_on_begin_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    std::optional<requeue_directory_access_attempt>
    on_directory_access_failure(std::filesystem::path const&             directory,
                                std::filesystem::filesystem_error const& error) override
    {
        m::dbg_format("Entering {}", __func__);
        std::ignore = directory;
        std::ignore = error;

        m_on_directory_access_failure_count.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    std::optional<requeue_file_access_attempt>
    on_file_access_failure(std::filesystem::path const&             directory,
                           std::filesystem::path const&             file,
                           std::filesystem::filesystem_error const& error) override
    {
        m::dbg_format("Entering {}", __func__);
        std::ignore = directory;
        std::ignore = file;
        std::ignore = error;

        m_on_directory_access_failure_count.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    void
    on_file_changed(std::filesystem::path const&, std::filesystem::path const&) override
    {
        // m::dbg_format("Entering {}", __func__);
        m_on_file_changed_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_file_deleted(std::filesystem::path const&, std::filesystem::path const&) override
    {
        // m::dbg_format("Entering {}", __func__);
        m_on_file_deleted_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_file_recheck_required(std::filesystem::path const&, std::filesystem::path const&) override
    {
        m::dbg_format("Entering {}", __func__);
        m_on_file_recheck_required_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_cancelled() override
    {
        m::dbg_format("Entering {}", __func__);
        m_on_cancelled_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_invalid() override
    {
        m::dbg_format("Entering {}", __func__);
        m_on_invalid_count.fetch_add(1, std::memory_order_relaxed);
        //
    }
};

//
// Implement std::formatter<> for these types so that we can compose these things together
// more simply
//

template <typename CharT>
struct std::formatter<change_notification_sink, CharT>
{
    using thetype = change_notification_sink;

    template <typename ParseContext>
    constexpr auto
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(thetype const& x, FormatContext& ctx) const
    {
        auto it = ctx.out();

        if constexpr (std::is_same_v<CharT, wchar_t>)
        {
            it = std::format_to(
                it,
                L"{{ m_on_begin_count: {}, m_on_directory_access_failure_count: {}, m_on_file_access_failure_count: {}, m_on_file_changed_count: {}, m_on_file_deleted_count: {}, m_on_file_recheck_required_count: {}, m_on_cancelled_count: {}, m_on_invalid_count: {} }}",
                x.m_on_begin_count.load(std::memory_order_relaxed),
                x.m_on_directory_access_failure_count.load(std::memory_order_relaxed),
                x.m_on_file_access_failure_count.load(std::memory_order_relaxed),
                x.m_on_file_changed_count.load(std::memory_order_relaxed),
                x.m_on_file_deleted_count.load(std::memory_order_relaxed),
                x.m_on_file_recheck_required_count.load(std::memory_order_relaxed),
                x.m_on_cancelled_count.load(std::memory_order_relaxed),
                x.m_on_invalid_count.load(std::memory_order_relaxed));
        }
        else if constexpr (std::is_same_v<CharT, char>)
        {
            it = std::format_to(
                it,
                "{{ m_on_begin_count: {}, m_on_directory_access_failure_count: {}, m_on_file_access_failure_count: {}, m_on_file_changed_count: {}, m_on_file_deleted_count: {}, m_on_file_recheck_required_count: {}, m_on_cancelled_count: {}, m_on_invalid_count: {} }}",
                x.m_on_begin_count.load(std::memory_order_relaxed),
                x.m_on_directory_access_failure_count.load(std::memory_order_relaxed),
                x.m_on_file_access_failure_count.load(std::memory_order_relaxed),
                x.m_on_file_changed_count.load(std::memory_order_relaxed),
                x.m_on_file_deleted_count.load(std::memory_order_relaxed),
                x.m_on_file_recheck_required_count.load(std::memory_order_relaxed),
                x.m_on_cancelled_count.load(std::memory_order_relaxed),
                x.m_on_invalid_count.load(std::memory_order_relaxed));
        }
        else
        {
            throw std::runtime_error("Unexpected CharT");
        }

        return it;
    }
};

TEST(Monitor, first)
{
    auto const td = m::googletest::create_temporary_directory();

    constexpr auto initial_contents = u8"Hello there filesystem!\n"sv;
    auto const     initial_span     = std::span(initial_contents.begin(), initial_contents.end());
    auto const     initial_bytes    = std::as_bytes(initial_span);

    auto const temporary_path = m::filesystem::make_path(td->path(), L"temporary_file");

    m::filesystem::store(temporary_path, initial_bytes);

    auto const monitor = m::filesystem::get_monitor();

    change_notification_sink cns;

    auto const token = monitor->register_watch(temporary_path, &cns);
#if 0
    // No test asserts, just execution.
    auto p     = m::filesystem::make_path("temporary_file");
    auto bytes = std::as_bytes(std::span(contents.begin(), contents.end()));

    m::filesystem::store(p, bytes);

    std::filesystem::remove(p);
#endif
}

TEST(Monitor, VerifyNoChange)
{
    auto const td = m::googletest::create_temporary_directory();

    constexpr auto initial_contents = u8"Hello there filesystem!\n"sv;
    auto const     initial_span     = std::span(initial_contents.begin(), initial_contents.end());
    auto const     initial_bytes    = std::as_bytes(initial_span);

    auto const temporary_path = m::filesystem::make_path(td->path(), L"temporary_file");

    m::filesystem::store(temporary_path, initial_bytes);

    auto const monitor = m::filesystem::get_monitor();

    change_notification_sink cns;

    {
        auto const token = monitor->register_watch(temporary_path, &cns);
    }

    EXPECT_EQ(cns.m_on_begin_count, 1);
    EXPECT_EQ(cns.m_on_directory_access_failure_count, 0);
    EXPECT_EQ(cns.m_on_file_access_failure_count, 0);
    EXPECT_EQ(cns.m_on_file_changed_count, 0);
    EXPECT_EQ(cns.m_on_file_deleted_count, 0);
    EXPECT_EQ(cns.m_on_cancelled_count, 1);

    // What else should be verified?
    //
    // The recheck event is permitted to be fired spuriously so we really can't have any
    // particular expectations on it. It probably *shouldn't* be firing during unit
    // testing but since we're not actually insulated against changes going on otherwise
    // on the system,

#if 0
    // No test asserts, just execution.
    auto p     = m::filesystem::make_path("temporary_file");
    auto bytes = std::as_bytes(std::span(contents.begin(), contents.end()));

    m::filesystem::store(p, bytes);

    std::filesystem::remove(p);
#endif
}

TEST(Monitor, MonitorNonExistentFile)
{
    auto const td = m::googletest::create_temporary_directory();

    constexpr auto initial_contents = u8"Hello there filesystem!\n"sv;
    auto const     initial_span     = std::span(initial_contents.begin(), initial_contents.end());
    auto const     initial_bytes    = std::as_bytes(initial_span);

    auto const temporary_path = m::filesystem::make_path(td->path(), L"temporary_file");

    auto const monitor = m::filesystem::get_monitor();

    change_notification_sink cns;

    {
        auto const token = monitor->register_watch(temporary_path, &cns);
        // Have to sleep to let the background threads attempt to access the
        // files
        std::this_thread::sleep_for(50ms);

        EXPECT_EQ(cns.m_on_begin_count, 1);
        EXPECT_EQ(cns.m_on_directory_access_failure_count, 0);
        // Monitoring a file may or may not actually access the file, so it's
        // possible that this may produce zero file access failures.
        EXPECT_LE(cns.m_on_file_access_failure_count, 1);
        EXPECT_EQ(cns.m_on_file_changed_count, 0);
        EXPECT_EQ(cns.m_on_file_deleted_count, 0);
        EXPECT_EQ(cns.m_on_cancelled_count, 0);

        m::filesystem::store(temporary_path, initial_bytes);

    }
}

#ifdef WIN32

TEST(Monitor, MonitorNonExistentSubdirectory)
{
    auto const td = m::googletest::create_temporary_directory();

    auto const path1 = m::filesystem::make_path(td->path(), L"path1");
    // Note that path2 is a subdirectory of path1, not a peer
    auto const path2 = m::filesystem::make_path(path1, L"path2");

    auto const monitor = m::filesystem::get_monitor();

    change_notification_sink cns;

    {
        auto const token = monitor->register_watch(path2, &cns);
        std::this_thread::sleep_for(50ms);
    }

    EXPECT_EQ(cns.m_on_begin_count, 1);
    EXPECT_EQ(cns.m_on_directory_access_failure_count, 1);
    EXPECT_EQ(cns.m_on_file_access_failure_count, 0);
    EXPECT_EQ(cns.m_on_file_changed_count, 0);
    EXPECT_EQ(cns.m_on_file_deleted_count, 0);
    EXPECT_EQ(cns.m_on_cancelled_count, 1);
}

#endif

#ifdef WIN32

TEST(Monitor, MonitorFileLifecycle)
{
    auto const td = m::googletest::create_temporary_directory();

    constexpr auto initial_contents = u8"Hello there filesystem!\n"sv;
    auto const     initial_span     = std::span(initial_contents.begin(), initial_contents.end());
    auto const     initial_bytes    = std::as_bytes(initial_span);

    auto const temporary_path = m::filesystem::make_path(td->path(), L"temporary_file");

    auto const monitor = m::filesystem::get_monitor();

    change_notification_sink cns;

    {
        auto const token = monitor->register_watch(temporary_path, &cns);
        // Have to sleep to let the background threads attempt to access the
        // files
        std::this_thread::sleep_for(50ms);

        EXPECT_EQ(cns.m_on_begin_count, 1);
        EXPECT_EQ(cns.m_on_directory_access_failure_count, 0);
        // Monitoring a file may or may not actually access the file, so it's
        // possible that this may produce zero file access failures.
        EXPECT_LE(cns.m_on_file_access_failure_count, 1);
        EXPECT_EQ(cns.m_on_file_changed_count, 0);
        EXPECT_EQ(cns.m_on_file_deleted_count, 0);
        EXPECT_EQ(cns.m_on_cancelled_count, 0);

        m::filesystem::store(temporary_path, initial_bytes);

        std::this_thread::sleep_for(50ms);

        EXPECT_EQ(cns.m_on_begin_count, 1);
        EXPECT_EQ(cns.m_on_directory_access_failure_count, 0);
        EXPECT_LE(cns.m_on_file_access_failure_count, 1);
        EXPECT_GT(cns.m_on_file_changed_count, 0);
        EXPECT_EQ(cns.m_on_file_deleted_count, 0);
        EXPECT_EQ(cns.m_on_cancelled_count, 0);

        // It's kind of weird but a single store may generate multiple change notifications
        // so we cannot expect simply "1" here. We will capture
        // the current value for future use.
        auto change_count = cns.m_on_file_changed_count.load();

        std::filesystem::remove(temporary_path);

        std::this_thread::sleep_for(50ms);

        EXPECT_EQ(cns.m_on_begin_count, 1);
        EXPECT_EQ(cns.m_on_directory_access_failure_count, 0);
        EXPECT_LE(cns.m_on_file_access_failure_count, 1);
        EXPECT_GE(cns.m_on_file_changed_count, change_count);
        EXPECT_EQ(cns.m_on_file_deleted_count, 1);
        EXPECT_EQ(cns.m_on_cancelled_count, 0);

        // We can wish that the change count hadn't gone up but that's
        // not necessarily the nature of these things. You don't know if
        // the filesystem or the C runtime library had changed some
        // file attributes before deleting the file, generating some
        // change notifications. As a result: new change count possibly.

        auto change_count_2 = cns.m_on_file_changed_count.load();

        m::filesystem::store(temporary_path, initial_bytes);

        std::this_thread::sleep_for(50ms);

        EXPECT_EQ(cns.m_on_begin_count, 1);
        EXPECT_EQ(cns.m_on_directory_access_failure_count, 0);
        EXPECT_LE(cns.m_on_file_access_failure_count, 1);
        EXPECT_GT(cns.m_on_file_changed_count, change_count_2);
        EXPECT_EQ(cns.m_on_file_deleted_count, 1);
        EXPECT_EQ(cns.m_on_cancelled_count, 0);
    }
}
#endif


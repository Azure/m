// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <filesystem>
#include <span>
#include <string_view>

#include <m/debugging/dbg_format.h>
#include <m/filesystem/filesystem.h>
#include <m/googletest/temporary_directory.h>

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
        m::dbg_format("Entering {}", __func__);
        m_on_file_changed_count.fetch_add(1, std::memory_order_relaxed);
        //
    }

    void
    on_file_deleted(std::filesystem::path const&, std::filesystem::path const&) override
    {
        m::dbg_format("Entering {}", __func__);
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

    m::filesystem::store(temporary_path, initial_bytes);

    auto const monitor = m::filesystem::get_monitor();

    change_notification_sink cns;

    {
        auto const token = monitor->register_watch(temporary_path, &cns);
    }

    EXPECT_EQ(cns.m_on_begin_count, 1);
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

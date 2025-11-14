// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <thread>

#include <m/pil/pil.h>
#include <m/pil/registry.h>
#include <m/print/print.h>
#include <m/tracing/cout_sink.h>
#include <m/tracing/tracing.h>

using namespace std::string_view_literals;
using namespace std::chrono_literals;

struct monitor_sink : public m::pil::iregistry_monitor_change_notification
{
    ~monitor_sink() = default;

    void
    on_begin(m::utc_time_point when) override
    {
        m::wtrace_error(L"on_begin({})", when);
        m_on_begins++;
        //
    }

    std::optional<requeue_key_access_attempt>
    on_key_access_failure(m::utc_time_point        when,
                          m::pil::key_path const&  key,
                          std::system_error const& ec) override
    {
        m::wtrace_error(L"on_key_access_failure({}, {}, {})",
                        when,
                        m::to_wstring(key.c_str()),
                        ec.code().value());
        m_on_key_access_failures++;
        return std::nullopt;
    }

    std::optional<requeue_change_notification_attempt>
    on_change_notification_attempt_failure(m::utc_time_point        when,
                                           m::pil::key_path const&  key,
                                           std::system_error const& ec) override
    {
        m::wtrace_error(L"on_change_notification_attempt_failure({}, {}, {})",
                        when,
                        m::to_wstring(key.c_str()),
                        ec.code().value());
        m_on_change_notification_attempt_failures++;
        return std::nullopt;
    }

    void
    on_change(m::utc_time_point when, m::pil::key_path const& key) override
    {
        m::wtrace_error(L"on_change({}, {})", when, m::to_wstring(key.c_str()));
        m_on_changes++;
    }

    void
    on_cancelled(m::utc_time_point when) override
    {
        m::wtrace_error(L"on_cancelled({})...", when);
        m_on_cancelleds++;
    }

    std::atomic<uintmax_t> m_on_begins;
    std::atomic<uintmax_t> m_on_key_access_failures;
    std::atomic<uintmax_t> m_on_change_notification_attempt_failures;
    std::atomic<uintmax_t> m_on_changes;
    std::atomic<uintmax_t> m_on_cancelleds;
};

TEST(DirectRegistryMonitoring, MonitorKey)
{
    auto coutsink = m::tracing::cout_sink::register_sink(m::tracing::diagnostic_channel_name,
                                                         m::tracing::monitor.get());

    m::wtrace_error(L"This is just a test");

    using namespace m::pil;
    auto p = make_platform();

    auto r  = p.get_registry();
    auto k1 = r.open_predefined_key(m::pil::predefined_key::current_user);
    auto k2 = k1.open_key(L"Software"sv);

    auto mon = r.monitor();

    std::random_device rd{};
    std::mt19937_64    gen(rd());

    auto r1 = gen();
    auto r2 = gen();

    auto name = std::format(L"temp_key_{}_{}", r1, r2);

    auto k3 = k2.create_key(std::wstring_view(name));

    monitor_sink sink;

    auto x = mon.register_watch(
        registry_monitor::register_watch_flags::value_changes, k3.get_path(), &sink);

    k3.set_string_value(L"SomeValue"sv, L"Value"sv);

    std::this_thread::sleep_for(50ms);

    EXPECT_EQ(sink.m_on_changes.load(), 1);

    x.reset();

    k2.delete_key(std::wstring_view(name));

    EXPECT_EQ(1, 1);
    //
}

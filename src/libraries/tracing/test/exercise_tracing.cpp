// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <chrono>
#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

#include <m/tracing/cout_sink.h>
#include <m/tracing/tracing.h>

using namespace std::chrono_literals;

TEST(Tracing, CreateSource)
{
    auto src = m::tracing::monitor.make_source();
}

TEST(Tracing, RegisterSink)
{
    auto coutsink = m::tracing::cout_sink::register_sink(&m::tracing::monitor);
}

TEST(Tracing, LogAnEventNoFormattingNoSinks)
{
    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::information, "Hello, tracing!");
}

TEST(Tracing, LogAnEventNoFormattingWithConsoleSink)
{
    auto coutsink = m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::information, "Hello, tracing!");
}

TEST(Tracing, LogATracingEventNoFormattingWithConsoleSink)
{
    auto coutsink = m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::tracing, "Hello, tracing this should not show up!");
}

TEST(Tracing, LogAErrorEventNoFormattingWithConsoleSink)
{
    auto coutsink = m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::error, "Hello, tracing this should definitely show up!");
}

TEST(Tracing, WLogAnEventNoFormattingNoSinks)
{
    auto src = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::information, L"Hello, tracing!");
}

TEST(Tracing, WLogAnEventNoFormattingWithConsoleSink)
{
    auto coutsink = m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::information, L"Hello, tracing!");
}

TEST(Tracing, WLogATracingEventNoFormattingWithConsoleSink)
{
    auto coutsink = m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::tracing, L"Hello, tracing this should not show up!");
}

TEST(Tracing, WLogAErrorEventNoFormattingWithConsoleSink)
{
    auto coutsink = m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

TEST(Tracing, LogMessagesAfterClosingSink)
{
    auto coutsink = m::tracing::cout_sink::register_sink(&m::tracing::monitor);
    auto src = m::tracing::monitor.make_source();

    src->wlog(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
    coutsink.reset();
    src->wlog(m::tracing::event_kind::error,
              L"This is another event but after the sink was closed");

}


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
    m::tracing::cout_sink::register_sink(&m::tracing::monitor);
}

TEST(Tracing, LogAnEventNoFormattingNoSinks)
{
    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::information, L"Hello, tracing!");
}

TEST(Tracing, LogAnEventNoFormattingWithConsoleSink)
{
    m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::information, L"Hello, tracing!");
}

TEST(Tracing, LogATracingEventNoFormattingWithConsoleSink)
{
    m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::tracing, L"Hello, tracing this should not show up!");
}

TEST(Tracing, LogAErrorEventNoFormattingWithConsoleSink)
{
    m::tracing::cout_sink::register_sink(&m::tracing::monitor);

    auto src = m::tracing::monitor.make_source();

    src->log(m::tracing::event_kind::error, L"Hello, tracing this should definitely show up!");
}

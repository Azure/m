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

#include <m/error_handling/macros.h>
#include <m/tracing/cout_sink.h>
#include <m/tracing/tracing.h>

using namespace std::chrono_literals;

TEST(InternalErrorCheck, First) 
{ 
    auto coutsink = m::tracing::cout_sink::register_sink(m::tracing::monitor.get());

    m::trace("Here's some tracing output!");

    M_INTERNAL_ERROR_CHECK(1 == 0); 
}


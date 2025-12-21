// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <gtest/gtest.h>

#include <format>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>

#include <m/tracing/ostream_sink.h>
#include <m/tracing/tracing.h>

using ::testing::EmptyTestEventListener;
using ::testing::InitGoogleTest;
using ::testing::Test;
using ::testing::TestEventListeners;
using ::testing::TestInfo;
using ::testing::TestPartResult;
using ::testing::UnitTest;

int
main(int argc, char** argv)
{
    std::cout << "*** Using googletest_main.cpp ***\n";

    InitGoogleTest(&argc, argv);

    auto coutsink = m::tracing::cout_sink::register_sink(m::tracing::diagnostic_channel_name,
                                                         m::tracing::monitor.get());

    int ret_val = RUN_ALL_TESTS();
    return ret_val;
}

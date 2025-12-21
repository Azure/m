// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <format>
#include <initializer_list>
#include <map>
#include <mutex>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <m/strings/literal_string_view.h>
#include <m/tracing/close_flush_option.h>
#include <m/tracing/ostream_sink.h>
#include <m/tracing/envelope.h>
#include <m/tracing/message_queue.h>
#include <m/tracing/sink.h>
#include <m/tracing/tracing.h>

using namespace m::string_view_literals;

namespace m
{
    namespace tracing
    {
        //
    } // namespace tracing
} // namespace m

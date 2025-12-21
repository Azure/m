// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/debugging.h>
#include <m/tracing/message.h>

namespace m::tracing
{
    gdsn
    get_next_gdsn()
    {
        static std::atomic<std::uint64_t> s_next_gdsn{};
        return static_cast<gdsn>(s_next_gdsn.fetch_add(1, std::memory_order_relaxed));
    }
} // namespace m::tracing

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <condition_variable>
#include <mutex>

#include <m/tracing/message_allocator.h>
#include <m/tracing/message_processor.h>
#include <m/tracing/message_source.h>

namespace m::tracing
{
    void
    return_to_sender(tracing::envelope const& env)
    {
        //
        auto msg = env.message();
        if (msg != nullptr)
        {
            env.message_source()->deallocate_message(msg);
        }
    }

} // namespace m::tracing

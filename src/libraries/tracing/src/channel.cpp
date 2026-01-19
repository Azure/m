// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <mutex>

#include <m/tracing/channel.h>

namespace m::tracing
{
    channel::channel(std::wstring_view name): m_name(name)
    {
        //
    }
} // namespace m::tracing

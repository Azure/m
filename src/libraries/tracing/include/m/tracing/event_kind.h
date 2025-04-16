// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdint>

namespace m
{
    namespace tracing
    {
        enum class event_kind : uint8_t
        {
            critical,    // to be called out specifically
            error,       // obvious
            information, // general useful operational information
            verbose,     // more useful information not on by default
            tracing,     // sometimes called "painful" - tracing in great detail
        };
    } // namespace tracing
} // namespace m

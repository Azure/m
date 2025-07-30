// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

namespace m
{
    //
    // Tag type and value to indicate a function signature
    // where the target object is already locked.
    //
    struct locked_t
    {
        explicit locked_t() = default;
    };

    constexpr locked_t locked{};
}
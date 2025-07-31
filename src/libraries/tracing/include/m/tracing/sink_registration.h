// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

namespace m::tracing
{
    /// <summary>
    /// The `sink_registration` struct is an empty struct that is used
    /// as a unique type returned when registering a tracing event sink
    /// with the tracing facility.
    ///
    /// Registering the sink returns a
    /// `std::unique_ptr<sink_registration>`. To "unregister" the sink,
    /// the client should allow the `std::unique_ptr<>` to be destroyed
    /// or call `.reset()` on it.
    /// </summary>
    struct sink_registration
    {
        virtual ~sink_registration() {}
    };

} // namespace m::tracing

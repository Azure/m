// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <chrono>

namespace m
{
    /// <summary>
    /// The standard duration quantum for m is milliseconds.
    /// 
    /// Originally there were going to be two. But wait if there are two, one for
    /// inwards, and one for outwards in API calls, maybe there needs to be a third
    /// for storing?
    /// 
    /// And then guidelines for dealing with precision loss and overflow handling.
    /// 
    /// Madness.
    /// 
    /// So we have one. Microseconds would feel a little better since computers are
    /// so fast, but it would also cut down on the time spans that durations could
    /// cover too much. An ideal case would be a
    /// std::chrono::duration<int128_t, std::micros> but 128 bit integers are not
    /// standard.
    /// 
    /// In any case - be sure to use the symbolic name, and whenever you actually
    /// do want say the count of milliseconds, use a
    /// duration_cast<std::chrono::milliseconds>() first so that you know what you
    /// have. duration_cast<>() is authored correctly so that if the source and
    /// destination types are the same, it is trivial.
    /// </summary>
    using duration = std::chrono::milliseconds;

}
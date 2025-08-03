// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <chrono>

namespace m
{
    //
    // In the C++ algorithms library, the execution policy for
    // algorithms are modeled as different fundamental types so
    // that varying overloads can be used to drive different
    // implementations of algorithms for, for example, sequetial
    // implementation of std::sort vs. a parallel implementation
    // thereof.
    // 
    // It is tempting to follow suit here, and have both a traits
    // type and a parameter values type that is dependent on the
    // execution policy / traits type to be able to give what would
    // normally be the more detail parameters one might give to a
    // "task scheduler".
    // 
    // On the other hand, at the current time, the primary target is
    // a work queue that dispatches work items in parallel with the
    // notion that being able to execute them in sequence could possibly
    // be of interest to some people so it might be useful to design
    // it in from the beginning.
    // 
    // This can be addressed by using a std::variant<> also but again, this
    // seems like overkill since switching on the discriminator in a
    // std::variant is obnoxiously difficult.
    // 
    // Points to consider in the future.
    //

    enum class work_queue_execution_policy
    {
        sequenced,
        parallel,
    };
} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <m/utility/compiler.h>

#include <cstdint>

namespace m
{
    namespace tracing
    {
        //
        //
        // The debugging for the tracing infrastructure requires a recompile but
        // one of the things you can do is enable a global sequence number to be
        // applied to all operations. This is maintained by a std::atomic<uint64_t>
        // in one of the .cpp TUs in the m_tracing library, monotonically
        // increasing, which can be stored in various places.
        //
        // We use a scoped enum since that's the recommended way to make these kinds
        // of "tagged integers". The scoped enum technique is really not very great
        // for most uses of specialized integers because of how poorly it integrates
        // with operator overloading and the like, but in our case, this is only
        // a counter. It's not impossible that code might want to compare for equality
        // but even that is really not right, and even so, it works for that.
        //
        //

        // gdsn == global debuging sequence number. If there was a reasonable
        // 4-8 character expanded name it would be used but hopefully this will
        // be memorable at least.
        enum class gdsn : std::uint64_t
        {
        };

        gdsn
        get_next_gdsn();

    } // namespace tracing
} // namespace m

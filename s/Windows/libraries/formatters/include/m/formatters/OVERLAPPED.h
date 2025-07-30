// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <format>

#include <m/cast/to.h>

#include <Windows.h>

#include "NTSTATUS.h"

typedef _Return_type_success_(return >= 0) LONG NTSTATUS;

template <typename CharT>
struct std::formatter<OVERLAPPED, CharT>
{
    template <typename ParseContext>
    constexpr ParseContext::iterator
    parse(ParseContext& ctx)
    {
        auto       it  = ctx.begin();
        auto const end = ctx.end();

        if (it != end && *it != '}')
            throw std::format_error("Invalid format string");

        return it;
    }

    template <typename FormatContext>
    FormatContext::iterator
    format(OVERLAPPED const& o, FormatContext& ctx) const
    {
        // Pull apart the OVERLAPPED into its component pieces:
        //
        auto const s = static_cast<NTSTATUS>(o.Internal);
        auto const bytesTransferred = m::to<uint64_t>(o.InternalHigh);
        auto const offset = static_cast<uint64_t>(o.Offset) | (static_cast<uint64_t>(o.OffsetHigh) << 32);

        return std::format_to(
            ctx.out(),
            L"{{ OVERLAPPED Status: {}, BytesTransferred: {}, Offset: {}, Handle: 0x{:x} }}",
            fmtNTSTATUS{s},
            bytesTransferred,
            offset,
            reinterpret_cast<uintptr_t>(o.hEvent));
    }
};

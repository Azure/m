// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <format>

#include <Windows.h>

#include "FILETIME.h"
#include "FILE_ACTION.h"

template <typename CharT>
struct std::formatter<FILE_NOTIFY_INFORMATION, CharT>
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
    format(FILE_NOTIFY_INFORMATION const& fni, FormatContext& ctx) const
    {
        fmtFILE_ACTION fa{fni.Action};
        wstring_view   fileName(fni.FileName, fni.FileNameLength / sizeof(WCHAR));

        return std::format_to(ctx.out(),
                              L"{{ NextEntryOffset: {}, Action: {}, FileName: \"{}\" }}",
                              fni.NextEntryOffset,
                              fa,
                              wstring_view(fni.FileName, fni.FileNameLength / sizeof(WCHAR)));
    }
};

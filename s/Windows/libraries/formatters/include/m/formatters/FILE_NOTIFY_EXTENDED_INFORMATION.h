// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <format>

#include <Windows.h>

#include "FILETIME.h"
#include "FILE_ACTION.h"

template <typename CharT>
struct std::formatter<FILE_NOTIFY_EXTENDED_INFORMATION, CharT>
{
    template <typename ParseContext>
    constexpr auto
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
    format(FILE_NOTIFY_EXTENDED_INFORMATION const& fnei, FormatContext& ctx) const
    {
        fmtFILE_ACTION    fa{fnei.Action};
        std::wstring_view fileName(fnei.FileName, fnei.FileNameLength / sizeof(WCHAR));

        auto it = ctx.out();

        it = std::format_to(
            it,
            L"{{ NextEntryOffset: {}, Action: {}, CreationTime: {}, LastModificationTime: {}, LastChangeTime: {}, LastAccessTime: {}, AllocatedLength: {}, FileSize: {}, FileAttributes: {:x}, ReparsePointTag: {}, FileId: {}, ParentFileId: {}, FileName: \"{}\" }}",
            fnei.NextEntryOffset,
            fa,
            fmtFILETIME(toFILETIME(fnei.CreationTime)),
            fmtFILETIME(toFILETIME(fnei.LastModificationTime)),
            fmtFILETIME(toFILETIME(fnei.LastChangeTime)),
            fmtFILETIME(toFILETIME(fnei.LastAccessTime)),
            fnei.AllocatedLength.QuadPart,
            fnei.FileSize.QuadPart,
            fnei.FileAttributes,
            fnei.ReparsePointTag,
            fnei.FileId.QuadPart,
            fnei.ParentFileId.QuadPart,
            std::wstring_view(fnei.FileName, fnei.FileNameLength / sizeof(WCHAR)));

        return it;
    }
};

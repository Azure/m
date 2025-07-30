// Copyright (c) Microsoft Corporation. All rights reserved.

#pragma once

#include <format>

#include <Windows.h>

struct fmtFILE_ACTION
{
    DWORD file_action;
};

template <typename CharT>
struct std::formatter<fmtFILE_ACTION, CharT>
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
    format(fmtFILE_ACTION action, FormatContext& ctx) const
    {
        wstring_view text{};

        switch (action.file_action)
        {
            case FILE_ACTION_ADDED: text = L"FILE_ACTION_ADDED"sv; break;
            case FILE_ACTION_REMOVED: text = L"FILE_ACTION_REMOVED"sv; break;
            case FILE_ACTION_MODIFIED: text = L"FILE_ACTION_MODIFIED"sv; break;
            case FILE_ACTION_RENAMED_OLD_NAME: text = L"FILE_ACTION_RENAMED_OLD_NAME"sv; break;
            case FILE_ACTION_RENAMED_NEW_NAME: text = L"FILE_ACTION_RENAMED_NEW_NAME"sv; break;
            default: return std::format_to(ctx.out(), L"Unmapped action {}", action.file_action);
        }

        return std::copy(text.begin(), text.end(), ctx.out());
    }
};

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <string_view>

#include <m/utf/transcode.h>

#include "convert.h"

//
// This header defines functions in the m namespace which will provide
// fluent conversion between the strongly typed UTF-8/UTF-16/UTF-32 and
// both char on the assumption that char is encoded using CP_ACP and
// wchar_t on the assumption that wchar_t is encoded as UTF-16.
//
// These are, more or less, the tacit assumptions on Windows.
//

namespace m
{
    void
    to_string(std::u8string_view v, std::string& str);

    std::string
    to_string(std::u8string_view v);

    void
    to_string(std::u16string_view v, std::string& str);

    std::string
    to_string(std::u16string_view v);

    void
    to_string(std::u32string_view v, std::string& str);

    std::string
    to_string(std::u32string_view v);

    void
    to_string(std::wstring_view v, std::string& str);

    std::string
    to_string(std::wstring_view v);

    void
    to_u8string(std::string_view v, std::u8string& str);

    std::u8string
    to_u8string(std::string_view v);

    void
    to_u16string(std::string_view v, std::u16string& str);

    std::u16string
    to_u16string(std::string_view v);

    void
    to_u32string(std::string_view v, std::u32string& str);

    std::u32string
    to_u32string(std::string_view v);

    void
    to_wstring(std::string_view v, std::wstring& str);

    std::wstring
    to_wstring(std::string_view v);

} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <string>
#include <string_view>

namespace m::multi_byte::impl
{
    void
    acp_convert(std::string_view v, std::wstring& str);

    void
    acp_convert(std::string_view v, std::u8string& str);

    void
    acp_convert(std::string_view v, std::u16string& str);

    void
    acp_convert(std::string_view v, std::u32string& str);

    void
    acp_convert(std::wstring_view v, std::string& str);

    void
    acp_convert(std::u8string_view v, std::string& str);

    void
    acp_convert(std::u16string_view v, std::string& str);

    void
    acp_convert(std::u32string_view v, std::string& str);

} // namespace m::multi_byte::impl
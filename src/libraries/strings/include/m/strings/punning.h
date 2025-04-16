// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstddef>
#include <string_view>

namespace m
{
    namespace strings
    {
        using byte_string_view = std::basic_string_view<std::byte>;

        byte_string_view
        as_byte_string_view(std::u8string_view const& view);

        byte_string_view
        as_byte_string_view(std::string_view const& view);

        //
        // Linux equates char with char8_t and wchar_t with char32_t, so
        // we will take the bull by the horns and use these punnings to force
        // use of the Utf-8 conversions on Linux.
        //
        // Windows, unfortunately, is still left adrift w.r.t. what to do
        // about char to wchar_t.
        //

        std::u8string_view
        as_u8string_view(std::string_view const& view);

#ifndef WIN32
        std::u32string_view
        as_u32string_view(std::wstring_view const& view);
#endif
    } // namespace strings
} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <filesystem>
#include <string_view>

namespace m
{
    namespace filesystem
    {
        //
        // Hide the gory details of mapping character sets
        //

        std::filesystem::path
        make_path(std::string_view v);

        std::filesystem::path
        make_path(std::wstring_view v);

        std::filesystem::path
        make_path(std::u8string_view v);

        std::filesystem::path
        make_path(std::u16string_view v);

        std::filesystem::path
        make_path(std::u32string_view v);

        //
        // Note that on Windows, char based access to paths uses a character
        // encoding that is lossy and may lose some Unicode characters.
        //
        // Code is strongly encouraged to use std::u8string as a cross
        // platform uniform way to consistently denote UTF-8 encoding
        // for their strings and this library will facilitate this with
        // the path libraries on Windows and Linux.
        //

        std::string
        path_to_string(std::filesystem::path const& p);

        void
        path_to_string(std::filesystem::path const& p, std::string& str);

        std::wstring
        path_to_wstring(std::filesystem::path const& p);

        void
        path_to_wstring(std::filesystem::path const& p, std::wstring& str);

        std::u8string
        path_to_u8string(std::filesystem::path const& p);

        void
        path_to_u8string(std::filesystem::path const& p, std::u8string& str);

        std::u16string
        path_to_u16string(std::filesystem::path const& p);

        void
        path_to_u16string(std::filesystem::path const& p, std::u16string& str);

        std::u32string
        path_to_u32string(std::filesystem::path const& p);

        void
        path_to_u32string(std::filesystem::path const& p, std::u32string& str);

    } // namespace filesystem
} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <filesystem>
#include <string_view>

#include <m/cast/to.h>
#include <m/strings/convert.h>

//
// The filesystem paths are platform specific so it's more or less required
// to have the platform specific conversion libraries in scope.
//
#ifdef WIN32
#include <m/windows_strings/convert.h>
#else
#include <m/linux_strings/convert.h>
#endif

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

    //
    // Path casts so that m::to<std::string>(std::filesystem::path const&) works
    //

    namespace details
    {
        using filesystem_string_type = std::filesystem::path::string_type;

        template <typename TTo>
        struct FilesystemPathCastHelper;

        template <>
        struct FilesystemPathCastHelper<std::string>
        {
            static std::string
            do_to(filesystem_string_type const& p)
            {
                return m::to_string(p);
            }
        };

        template <>
        struct FilesystemPathCastHelper<std::wstring>
        {
            static std::wstring
            do_to(filesystem_string_type const& p)
            {
                return m::to_wstring(p);
            }
        };

        template <>
        struct FilesystemPathCastHelper<std::u8string>
        {
            static std::u8string
            do_to(filesystem_string_type const& p)
            {
                return m::to_u8string(p);
            }
        };

        template <>
        struct FilesystemPathCastHelper<std::u16string>
        {
            static std::u16string
            do_to(filesystem_string_type const& p)
            {
                return m::to_u16string(p);
            }
        };

        template <>
        struct FilesystemPathCastHelper<std::u32string>
        {
            static std::u32string
            do_to(filesystem_string_type const& p)
            {
                return m::to_u32string(p);
            }
        };
    } // namespace details

    template <typename TTo, typename TFrom>
        requires std::is_same_v<std::remove_cvref_t<TFrom>, std::filesystem::path>
    TTo
    to(TFrom const& from)
    {
        auto c_str = from.c_str();
        return details::FilesystemPathCastHelper<TTo>::do_to(c_str);
    }

} // namespace m

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <filesystem>
#include <string_view>

#include <m/cast/to.h>
#include <m/strings/convert.h>
#include <m/strings/tstring.h>
#include <m/utf/decode.h>
#include <m/utf/encode.h>

//
// The filesystem paths are platform specific so it's more or less required
// to have the platform specific conversion libraries in scope.
//
#ifdef WIN32

#include <m/windows_strings/convert.h>

#define _M_FILESYSTEM_T(x) L##x

#else

#include <m/linux_strings/convert.h>

#define _M_FILESYSTEM_T(x) x

#endif

#define M_FILESYSTEM_T(x) _M_FILESYSTEM_T(x)

namespace m
{
    namespace filesystem
    {
        using path_value_type  = std::filesystem::path::value_type;
        using path_string_view = std::basic_string_view<path_value_type>;
        using path_string      = std::basic_string<path_value_type>;

        /// <summary>
        /// Constructs a path by combining:
        ///
        /// 1. The current working directory
        /// 2. An optional base path
        /// 3. An overlay path
        ///
        /// If the base path is present and is absolute, the current
        /// working directory is not referenced.
        ///
        /// The intent of this API is that it can be used
        /// with, for example, a command line tool in the
        /// form:
        ///
        /// toolname -out .\my\outputs inputfile.xyz
        ///
        /// The tool would generate the output filename
        /// by passing a std::filesystem::path(".\my\outputs")
        /// in the base_path parameter and "inputfile.out"
        /// in the overlay_path parameter.
        ///
        /// </summary>
        /// <param name="base_path"></param>
        /// <param name="overlay_path"></param>
        /// <returns></returns>
        std::filesystem::path
        combine(std::optional<std::filesystem::path>&& base_path,
                std::filesystem::path const&           overlay_path);

        std::filesystem::path
        combine(std::optional<std::filesystem::path> const& base_path,
                std::filesystem::path const&                overlay_path);

        /// <summary>
        /// Performs path combination, and replaces the extension.
        /// </summary>
        /// <param name="base_path"></param>
        /// <param name="overlay_path"></param>
        /// <param name="replacement"></param>
        /// <returns></returns>
        std::filesystem::path
        combine(std::optional<std::filesystem::path>&& base_path,
                std::filesystem::path const&           overlay_path,
                std::filesystem::path const&           replacement_extension);

        std::filesystem::path
        combine(std::optional<std::filesystem::path> const& base_path,
                std::filesystem::path const&                overlay_path,
                std::filesystem::path const&                replacement_extension);

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

        std::filesystem::path
        make_path(std::filesystem::path const&, std::string_view v);

        std::filesystem::path
        make_path(std::filesystem::path const&, std::wstring_view v);

        std::filesystem::path
        make_path(std::filesystem::path const&, std::u8string_view v);

        std::filesystem::path
        make_path(std::filesystem::path const&, std::u16string_view v);

        std::filesystem::path
        make_path(std::filesystem::path const&, std::u32string_view v);

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

        //
        void
        to_native(std::string_view v, std::basic_string<std::filesystem::path::value_type>& str);

        std::basic_string<std::filesystem::path::value_type>
        to_native(std::string_view v);

        void
        to_native(std::wstring_view v, std::basic_string<std::filesystem::path::value_type>& str);

        std::basic_string<std::filesystem::path::value_type>
        to_native(std::wstring_view v);

        void
        to_native(std::u8string_view v, std::basic_string<std::filesystem::path::value_type>& str);

        std::basic_string<std::filesystem::path::value_type>
        to_native(std::u8string_view v);

        void
        to_native(std::u16string_view v, std::basic_string<std::filesystem::path::value_type>& str);

        std::basic_string<std::filesystem::path::value_type>
        to_native(std::u16string_view v);

        void
        to_native(std::u32string_view v, std::basic_string<std::filesystem::path::value_type>& str);

        std::basic_string<std::filesystem::path::value_type>
        to_native(std::u32string_view v);

        template <typename T>
        inline decltype(auto)
        to_path_string(T&& v)
        {
            return m::to_basic_string_t<path_value_type>(std::forward<T>(v));
        }

    } // namespace filesystem

    //
    // Path casts so that m::to<std::string>(std::filesystem::path const&) works
    //

    namespace details
    {
        using filesystem_string_type = std::filesystem::path::string_type;
        using filesystem_string_view_type =
            std::basic_string_view<std::filesystem::path::value_type>;

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
        requires std::is_same_v<TFrom, std::filesystem::path>
    TTo
    to(TFrom const& from)
    {
        auto const native = from.native();
        return details::FilesystemPathCastHelper<TTo>::do_to(native);
    }

} // namespace m

//
// For the std::formatter<> specialization, we have specific capability to
// disable via a macro since it's possible that at some point an overload
// could be provided by someone else (both std::formatter and
// std::filesystem::path are not in our namespace; we are only providing
// this as a convenience) and we should back off.
//
#ifndef M_DISABLE_FILESYSTEM_PATH_FORMATTER

template <typename TChar>
struct std::formatter<std::filesystem::path, TChar>
{
    template <typename ParseContext>
    constexpr decltype(auto)
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
    format(std::filesystem::path const& p, FormatContext& ctx) const
    {
        auto  out    = ctx.out();
        auto& native = p.native();
        auto  it     = native.cbegin();
        auto  end    = native.cend();

        while (it != end)
        {
            auto [newit, ch] = m::utf::decode_utf(std::filesystem::path::value_type{}, it, end);
            out              = m::utf::encode_char<TChar>(ch, out);
            it               = newit;
        }

        return out;
    }
};

#endif

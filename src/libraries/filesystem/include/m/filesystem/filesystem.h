// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include <m/byte_streams/byte_streams.h>

namespace m
{
    namespace filesystem
    {
        class file
        {
        public:
            std::filesystem::path
            path()
            {
                return do_path();
            }

        protected:
            virtual std::filesystem::path
            do_path() = 0;
        };

        class seekable_input_file :
            public m::filesystem::file,
            public m::byte_streams::ra_in,
            public m::byte_streams::seq_in,
            public m::byte_streams::seekable
        {
        public:
        };

        class seekable_output_file :
            public m::filesystem::file,
            public m::byte_streams::ra_out,
            public m::byte_streams::seq_out,
            public m::byte_streams::seekable
        {
        public:
        };

        std::shared_ptr<seekable_input_file>
        open_seekable_input_file(std::filesystem::path const& path);

        std::shared_ptr<seekable_output_file>
        open_seekable_output_file(std::filesystem::path const& path);

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
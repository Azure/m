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
    } // namespace filesystem
} // namespace m
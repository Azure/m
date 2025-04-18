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
        class file : public byte_streams::ra_in, public byte_streams::seq_in
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

        std::shared_ptr<file>
        open(std::filesystem::path const& path);

        //
        // Hide the gory details of mapping character sets
        //

        std::filesystem::path
        make_path(std::string_view v);

        std::filesystem::path
        make_path(std::wstring_view v);
    } // namespace filesystem
} // namespace m
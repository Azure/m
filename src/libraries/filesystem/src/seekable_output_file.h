// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#pragma once

#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>

#include <m/filesystem/filesystem.h>

namespace m
{
    namespace filesystem_impl
    {
        class seekable_output_file : public m::filesystem::seekable_output_file
        {
        public:
            seekable_output_file(std::filesystem::path const& path);
            seekable_output_file(seekable_output_file const&) = delete;
            seekable_output_file(seekable_output_file&&) noexcept;

            seekable_output_file&
            operator=(seekable_output_file const&) = delete;

            void
            operator=(seekable_output_file&&) = delete;

            ~seekable_output_file();

        protected:
            std::filesystem::path do_path() override;

            // byte_streams::seq_in
            void
            do_write(std::span<std::byte const> s) override;

            // byte_streams::ra_in
            void
            do_write(io::position_t p, std::span<std::byte const> s) override;

            void
            do_seek(io::position_t p) override;

            void
            do_seek(io::offset_t o) override;

            io::position_t
            do_tell() override;

            void
            seek(long offset, int origin);

            void
            write(std::span<std::byte const> s);

            void
            seek_to(io::position_t p);

            std::mutex            m_mutex;
            std::FILE*            m_fp;
            std::filesystem::path m_path;
        };
    } // namespace filesystem_impl
} // namespace m

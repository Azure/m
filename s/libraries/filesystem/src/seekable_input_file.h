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
        class seekable_input_file : public m::filesystem::seekable_input_file
        {
        public:
            seekable_input_file(std::filesystem::path const& path);
            seekable_input_file(seekable_input_file const&) = delete;
            seekable_input_file(seekable_input_file&&) noexcept;

            seekable_input_file&
            operator=(seekable_input_file const&) = delete;

            void
            operator=(seekable_input_file&&) = delete;

            ~seekable_input_file();

        protected:
            std::filesystem::path do_path() override;

            // byte_streams::seq_in
            size_t
            do_read(std::span<std::byte>& span) override;

            // byte_streams::ra_in
            size_t
            do_read(io::position_t p, std::span<std::byte>& span) override;

            void
            do_seek(io::position_t p) override;

            void
            do_seek(io::offset_t o) override;

            io::position_t
            do_tell() override;

            void
            seek(long offset, int origin);

            std::size_t
            read(std::span<std::byte>& span);

            void
            seek_to(io::position_t p);

            std::mutex            m_mutex;
            std::FILE*            m_fp;
            std::filesystem::path m_path;
        };
    } // namespace filesystem_impl
} // namespace m

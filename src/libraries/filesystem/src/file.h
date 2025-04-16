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
        class file : public m::filesystem::file
        {
        public:
            file(std::filesystem::path const& path);
            file(file const&) = delete;
            file(file&&) noexcept;

            file&
            operator=(file const&) = delete;

            void
            operator=(file&&) = delete;

            ~file();

        protected:
            std::filesystem::path do_path() override;

            // byte_streams::seq_in
            size_t
            do_read(gsl::span<std::byte>& span) override;

            // byte_streams::ra_in
            size_t
            do_read(io::position_t p, gsl::span<std::byte>& span) override;

            void
            seek(long offset, int origin);

            std::size_t
            read(gsl::span<std::byte>& span);

            void
            seek_to(io::position_t p);

            std::mutex            m_mutex;
            std::FILE*            m_fp;
            std::filesystem::path m_path;
        };
    } // namespace filesystem_impl
} // namespace m

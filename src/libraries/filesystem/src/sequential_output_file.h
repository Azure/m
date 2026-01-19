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
        class sequential_output_file : public m::filesystem::sequential_output_file
        {
        public:
            sequential_output_file(std::filesystem::path const& path);
            sequential_output_file(sequential_output_file const&) = delete;
            sequential_output_file(sequential_output_file&&) noexcept;

            sequential_output_file&
            operator=(sequential_output_file const&) = delete;

            void
            operator=(sequential_output_file&&) = delete;

            ~sequential_output_file();

        protected:
            std::filesystem::path
            do_path() override;

            void
            do_write(std::span<std::byte const> s) override;

            std::mutex            m_mutex;
            std::FILE*            m_fp;
            std::filesystem::path m_path;
        };
    } // namespace filesystem_impl
} // namespace m

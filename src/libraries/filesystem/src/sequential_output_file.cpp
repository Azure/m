// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include <m/cast/to.h>
#include <m/filesystem/filesystem.h>

#include "sequential_output_file.h"

m::filesystem_impl::sequential_output_file::sequential_output_file(std::filesystem::path const& path):
    m_fp(nullptr), m_path(path)
{
    auto const s = path.c_str();
    m_fp =
#ifdef WIN32
        _wfopen(s, L"wb");
#else
        std::fopen(s, "wb");
#endif
    if (!m_fp)
        throw std::runtime_error("unable to open output file");
}

m::filesystem_impl::sequential_output_file::sequential_output_file(
    sequential_output_file&& other) noexcept:
    m_fp(nullptr)
{
    using std::swap;

    swap(m_fp, other.m_fp);
    swap(m_path, other.m_path);
}

m::filesystem_impl::sequential_output_file::~sequential_output_file()
{
    if (m_fp)
        fclose(std::exchange(m_fp, nullptr));
}

std::filesystem::path
m::filesystem_impl::sequential_output_file::do_path()
{
    auto const l = std::unique_lock(m_mutex);
    return m_path;
}

// byte_streams::seq_in
void
m::filesystem_impl::sequential_output_file::do_write(std::span<std::byte const> span)
{
    auto const l = std::unique_lock(m_mutex);
    write(span);
}


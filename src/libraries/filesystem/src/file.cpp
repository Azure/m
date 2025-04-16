// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include <m/cast/to.h>
#include <m/filesystem/filesystem.h>

#include "file.h"

m::filesystem_impl::file::file(std::filesystem::path const& path): m_fp(nullptr), m_path(path)
{
    auto const s = path.c_str();
    m_fp =
#ifdef WIN32
        _wfopen(s, L"rb");
#else
        std::fopen(s, "rb");
#endif
    if (!m_fp)
        throw std::runtime_error("unable to open input file");
}

m::filesystem_impl::file::file(file&& other) noexcept: m_fp(nullptr)
{
    using std::swap;

    swap(m_fp, other.m_fp);
    swap(m_path, other.m_path);
}

m::filesystem_impl::file::~file()
{
    if (m_fp)
        fclose(std::exchange(m_fp, nullptr));
}

std::filesystem::path
m::filesystem_impl::file::do_path()
{
    auto const l = std::unique_lock(m_mutex);
    return m_path;
}

// byte_streams::seq_in
std::size_t
m::filesystem_impl::file::do_read(std::span<std::byte>& span)
{
    auto const l = std::unique_lock(m_mutex);
    return read(span);
}

// byte_streams::ra_in
std::size_t
m::filesystem_impl::file::do_read(io::position_t p, std::span<std::byte>& span)
{
    auto const l = std::unique_lock(m_mutex);
    seek_to(p);
    return read(span);
}

std::size_t
m::filesystem_impl::file::read(std::span<std::byte>& span)
{
    auto const count = std::fread(span.data(), sizeof(std::byte), span.size(), m_fp);
    if (count == 0)
    {
        if (std::ferror(m_fp))
        {
            throw std::runtime_error("error reading from file");
        }

        if (std::feof(m_fp))
        {
            span = std::span<std::byte>{};
            return 0;
        }
    }

    span = span.subspan(0, count);
    return count;
}

void
m::filesystem_impl::file::seek(long offset, int origin)
{
    if (std::fseek(m_fp, offset, origin))
        throw std::runtime_error("error seeking file");
}

void
m::filesystem_impl::file::seek_to(io::position_t p)
{
    //
    // The C/C++ API doesn't give a way to directly seek past 2^32-1 bytes so if the
    // desired position is that far ahead, we will have to seek ahead incrementally.
    //
    // This is because the type that could do the seek in a single call uses fpos_t
    // which is opaque and is larger than 64 bits.
    //
    // ("long" is relevant here because it's the "offset" parameter to std::fseek().)
    //

    if (p < std::numeric_limits<long>::max())
    {
        // The simple case.
        seek(m::to<long>(std::to_underlying(p)), SEEK_SET);
        return;
    }

    // Now we burn down p to zero
    while (p != 0)
    {
        long offset{};

        if (p < std::numeric_limits<long>::max())
            offset = m::to<long>(std::to_underlying(p));
        else
            offset = std::numeric_limits<long>::max();

        seek(offset, SEEK_CUR);

        p -= offset;
    }
}

// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>

#include <m/cast/to.h>
#include <m/filesystem/filesystem.h>

#include "seekable_output_file.h"

m::filesystem_impl::seekable_output_file::seekable_output_file(std::filesystem::path const& path):
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

m::filesystem_impl::seekable_output_file::seekable_output_file(
    seekable_output_file&& other) noexcept:
    m_fp(nullptr)
{
    using std::swap;

    swap(m_fp, other.m_fp);
    swap(m_path, other.m_path);
}

m::filesystem_impl::seekable_output_file::~seekable_output_file()
{
    if (m_fp)
        fclose(std::exchange(m_fp, nullptr));
}

std::filesystem::path
m::filesystem_impl::seekable_output_file::do_path()
{
    auto const l = std::unique_lock(m_mutex);
    return m_path;
}

// byte_streams::seq_in
void
m::filesystem_impl::seekable_output_file::do_write(std::span<std::byte const> span)
{
    auto const l = std::unique_lock(m_mutex);
    write(span);
}

// byte_streams::ra_in
void
m::filesystem_impl::seekable_output_file::do_write(io::position_t             p,
                                                   std::span<std::byte const> span)
{
    auto const l = std::unique_lock(m_mutex);
    seek_to(p);
    write(span);
}

void
m::filesystem_impl::seekable_output_file::write(std::span<std::byte const> span)
{
    auto       l     = std::unique_lock(m_mutex);
    auto const count = std::fwrite(span.data(), sizeof(std::byte), span.size(), m_fp);
    if (count < span.size())
        throw std::runtime_error("Failed writing to file");
}

void
m::filesystem_impl::seekable_output_file::do_seek(io::position_t p)
{
    seek_to(p);
}

void
m::filesystem_impl::seekable_output_file::do_seek(io::offset_t o)
{
    auto const o1 = std::to_underlying(o);
    auto const o2 = m::to<long>(o1);
    auto       l  = std::unique_lock(m_mutex);
    seek(o2, SEEK_CUR);
}

m::io::position_t
m::filesystem_impl::seekable_output_file::do_tell()
{
    auto       l = std::unique_lock(m_mutex);
    auto const p = std::ftell(m_fp);
    if (p == -1L)
        throw std::runtime_error("Error getting file position");
    if (p < 0)
        throw std::runtime_error("ftell() returned a negative file position; impossible");
    return m::io::position_t{static_cast<unsigned long>(p)};
}

void
m::filesystem_impl::seekable_output_file::seek(long offset, int origin)
{
    if (std::fseek(m_fp, offset, origin))
        throw std::runtime_error("error seeking file");
}

void
m::filesystem_impl::seekable_output_file::seek_to(io::position_t p)
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

    if (p < (std::numeric_limits<long>::max)())
    {
        // The simple case.
        seek(m::to<long>(std::to_underlying(p)), SEEK_SET);
        return;
    }

    // Now we burn down p to zero
    while (p != 0)
    {
        long offset{};

        if (p < (std::numeric_limits<long>::max)())
            offset = m::to<long>(std::to_underlying(p));
        else
            offset = (std::numeric_limits<long>::max)();

        seek(offset, SEEK_CUR);

        p -= offset;
    }
}

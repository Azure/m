// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include <cstdint>
#include <cstdio>
#include <type_traits>

#include <m/cast/to.h>
#include <m/filesystem/filesystem_loadstore.h>
#include <m/utility/make_span.h>

#ifndef WIN32

namespace
{
    constexpr std::size_t k_chunk_size = 1u << 16;
}

namespace m::filesystem
{
    std::vector<std::byte>
    load(std::filesystem::path const& path)
    {
        std::vector<std::byte> result;

        auto fp = std::fopen(path.c_str(), "rb");
        if (!fp)
        {
            auto const saved_errno = errno;
            throw std::filesystem::filesystem_error(path.c_str(),
                                                    std::make_error_code(std::errc{saved_errno}));
        }

        std::array<std::byte, k_chunk_size> buffer;

        for (;;)
        {
            auto count_read = std::fread(buffer.data(), sizeof(std::byte), buffer.size(), fp);

            if (std::ferror(fp))
            {
                std::fclose(fp);
                throw std::runtime_error("error reading file");
            }

            auto span = m::make_span(buffer.data(), count_read);

#ifdef __cpp_lib_container_ranges
            result.append_range(span);
#else
            result.insert(result.end(), span.cbegin(), span.cend());
#endif

            if (std::feof(fp))
                break;
        }

        std::fclose(fp);

        return result;
    }

    std::optional<std::vector<std::byte>>
    load(std::filesystem::path const& path, std::error_code& ec)
    {
        std::vector<std::byte> result;

        auto const fp = std::fopen(path.c_str(), "rb");
        if (!fp)
        {
            auto const saved_errno = errno;
            ec                     = std::make_error_code(std::errc{saved_errno});
            return std::nullopt;
        }

        std::array<std::byte, k_chunk_size> buffer;

        for (;;)
        {
            auto count_read = std::fread(buffer.data(), sizeof(std::byte), buffer.size(), fp);

            if (std::ferror(fp))
            {
                std::fclose(fp);
                ec = std::make_error_code(std::errc{EIO});
                return std::nullopt;
            }

            auto span = m::make_span(buffer.data(), count_read);

#ifdef __cpp_lib_container_ranges
            result.append_range(span);
#else
            result.insert(result.end(), span.cbegin(), span.cend());
#endif

            if (std::feof(fp))
                break;
        }

        std::fclose(fp);

        return result;
    }

    void
    store_dynamic(std::filesystem::path const& path, std::span<std::byte const, std::dynamic_extent> data)
    {
        auto const fp = std::fopen(path.c_str(), "wb");
        if (!fp)
        {
            auto const saved_errno = errno;
            throw std::filesystem::filesystem_error(path.c_str(),
                                                    std::make_error_code(std::errc{saved_errno}));
        }

        auto const count =
            std::fwrite(data.data(), sizeof(std::remove_cvref_t<decltype(data)>::value_type), data.size(), fp);

        if (count < data.size())
        {
            auto const saved_errno = errno;
            std::fclose(fp);
            throw std::filesystem::filesystem_error(path.c_str(),
                                                    std::make_error_code(std::errc{saved_errno}));
        }

        std::fclose(fp);
    }

    void
    store_dynamic(std::filesystem::path const& path, std::span<std::byte const, std::dynamic_extent> data, std::error_code& ec)
    {
        auto const fp = std::fopen(path.c_str(), "wb");
        if (!fp)
        {
            auto const saved_errno = errno;
            ec                     = std::make_error_code(std::errc{saved_errno});
            return;
        }

        auto const count =
            std::fwrite(data.data(), sizeof(std::remove_cvref_t<decltype(data)>::value_type), data.size(), fp);

        if (count < data.size())
        {
            auto const saved_errno = errno;
            std::fclose(fp);
            ec = std::make_error_code(std::errc{saved_errno});
            return;
        }

        std::fclose(fp);
    }

} // namespace m::filesystem

#endif
